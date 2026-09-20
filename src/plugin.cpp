#include "display.h"
#include "client_display.h"
#include "pattern.h"
#include "player_info.h"

#include <safetyhook.hpp>
#include <ISmmPlugin.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <schemasystem/schemasystem.h>
#include <iserver.h>
#include <icvar.h>
#include <interfaces/interfaces.h>
#include <networkstringtabledefs.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

PLUGIN_GLOBALVARS();
static_assert(METAMOD_PLAPI_VERSION == 17, "Botmod must use the real Metamod API 17 headers");

namespace {

constexpr int kMaxPlayers = 64;
#ifdef _WIN32
constexpr const char* kServerModule = "server.dll";
constexpr const char* kPlatform = "windows";
#else
constexpr const char* kServerModule = "libserver.so";
constexpr const char* kPlatform = "linux";
#endif

struct Field {
    int offset;
    int size;
};

Field FindField(CSchemaSystemTypeScope* scope, const char* className, const char* fieldName, int expectedSize)
{
    auto* info = scope->FindDeclaredClass(className).Get();
    if (info) {
        for (int i = 0; i < info->m_nFieldCount; ++i) {
            const auto& field = info->m_pFields[i];
            if (std::strcmp(field.m_pszName, fieldName) != 0)
                continue;
            int size = 0;
            uint8 alignment = 0;
            if (!field.m_pType || !field.m_pType->GetSizeAndAlignment(size, alignment) ||
                size != expectedSize || field.m_nSingleInheritanceOffset < 0 ||
                field.m_nSingleInheritanceOffset > info->m_nSize - size)
                break;
            return {field.m_nSingleInheritanceOffset, size};
        }
    }
    throw std::runtime_error(std::string("Missing or incompatible schema field: ") + className + "::" + fieldName);
}

template<typename T>
T& At(CEntityInstance* entity, Field field)
{
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(entity) + field.offset);
}

// Check the full serial-numbered identity when restoring a snapshot: a reused
// slot must never receive the previous occupant's flags, name, or SteamID.
CEntityInstance* GetEntity(CEntitySystem* system, int index)
{
    if (!system || index < 1 || index >= MAX_TOTAL_ENTITIES)
        return nullptr;
    auto* chunk = system->m_EntityList.m_pIdentityChunks[index / MAX_ENTITIES_IN_LIST];
    if (!chunk)
        return nullptr;
    auto& identity = chunk[index % MAX_ENTITIES_IN_LIST];
    if (identity.GetEntityIndex().Get() != index ||
        (identity.m_flags & (EF_DELETE_IN_PROGRESS | EF_MARKED_FOR_DELETE)))
        return nullptr;
    return identity.m_pInstance;
}

CEntityInstance* GetPawn(CEntitySystem* system, CEntityInstance* controller, Field field)
{
    const auto handle = At<CEntityHandle>(controller, field);
    auto* pawn = GetEntity(system, handle.GetEntryIndex());
    if (!pawn || pawn->GetRefEHandle() != handle ||
        std::strcmp(pawn->GetClassname(), "player") != 0)
        return nullptr;
    return pawn;
}

class Botmod final : public ISmmPlugin, public IMetamodListener {
public:
    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;
    void OnLevelInit(const char*, const char*, const char*, const char*, bool, bool) override;
    bool Pause(char* error, size_t maxlen) override
    {
        std::snprintf(error, maxlen, "Unload Botmod to restore the native display");
        return false;
    }

    const char* GetAuthor() override { return "Botmod contributors"; }
    const char* GetName() override { return "Botmod"; }
    const char* GetDescription() override { return "Removes BOT name prefixes in the in-game player list"; }
    const char* GetURL() override { return ""; }
    const char* GetLicense() override { return "MIT"; }
    const char* GetVersion() override { return "1.1.1"; }
    const char* GetDate() override { return __DATE__; }
    const char* GetLogTag() override { return "BOTMOD"; }

private:
    static void PackEntities(void* server, void* snapshot, int count, void* clients, void* transmit);
    void PackPre();
    void PackPost();
    CEntitySystem* EntitySystem() const
    {
        CEntitySystem* result = nullptr;
        if (resources_)
            std::memcpy(&result, static_cast<const std::uint8_t*>(resources_) + entitySystemOffset_, sizeof(result));
        return result;
    }
    void Restore();
    void MarkDisplayChanged(CEntityInstance* entity) const;
    void OverridePawn(CEntityInstance* pawn);
    static void UserInfoChanged(CNetworkGameServerBase* server, CPlayerSlot slot);
    bool EnsureUserInfoHook();
    void* GetClient(CNetworkGameServerBase* server, int slot) const;
    std::uint64_t DisplayId(int slot, CEntityInstance* controller) const;
    void RefreshUserInfo();
    static void Status();
    std::optional<botmod::PlayerInfo> PublishedInfo(int slot) const;

    ICvar* cvar_ = nullptr;
    ConCommandRef statusCommand_;
    INetworkStringTableContainer* tables_ = nullptr;
    std::uint64_t packCalls_ = 0;
    std::uint64_t userInfoCalls_ = 0;
    std::uint64_t userInfoOverrides_ = 0;
    int lastControllers_ = 0;
    int lastPawns_ = 0;

    void* resources_ = nullptr;
    INetworkServerService* network_ = nullptr;
    botmod::ClientLayout clientLayout_;
    SafetyHookInline userInfoHook_;
    std::atomic<unsigned int> userInfoDepth_{0};
    bool refreshUserInfo_ = true;
    bool reportedUserInfo_ = false;
    bool reportedUserInfoFailure_ = false;
    Field flags_{};
    Field steamId_{};
    Field name_{};
    Field hltv_{};
    Field pawn_{};
    Field playerPawn_{};
    int entitySystemOffset_ = -1;
    SafetyHookInline packHook_;
    std::recursive_mutex packingMutex_;
    std::atomic<unsigned int> packingDepth_{0};
    bool reported_ = false;
    bool reportedPawns_ = false;

    template<typename Override>
    struct Snapshot {
        CEntityInstance* entity = nullptr;
        CEntityHandle handle;
        std::optional<Override> display;
    };
    std::array<Snapshot<botmod::DisplayOverride>, kMaxPlayers> snapshots_;
    // Active and playing pawns can differ when a controller is spectating.
    std::array<Snapshot<botmod::PawnDisplayOverride>, kMaxPlayers * 2> pawnSnapshots_;
    std::size_t pawnCount_ = 0;
};

Botmod g_Botmod;

bool Botmod::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool /*late*/)
{
    PLUGIN_SAVEVARS();
    clientLayout_ = {};
    entitySystemOffset_ = -1;
    try {
        auto factory = ismm->GetEngineFactory();
        resources_ = factory(GAMERESOURCESERVICESERVER_INTERFACE_VERSION, nullptr);
        network_ = static_cast<INetworkServerService*>(factory(NETWORKSERVERSERVICE_INTERFACE_VERSION, nullptr));
        cvar_ = static_cast<ICvar*>(factory(CVAR_INTERFACE_VERSION, nullptr));
        tables_ = static_cast<INetworkStringTableContainer*>(factory(INTERFACENAME_NETWORKSTRINGTABLESERVER, nullptr));
        auto* schema = static_cast<CSchemaSystem*>(factory(SCHEMASYSTEM_INTERFACE_VERSION, nullptr));
        if (!resources_ || !schema || !network_ || !cvar_ || !tables_)
            throw std::runtime_error("CS2 game resource or schema interface unavailable");
        auto* scope = schema->FindTypeScopeForModule(kServerModule);
        if (!scope)
            throw std::runtime_error("CS2 server schema scope unavailable");
        flags_ = FindField(scope, "CBaseEntity", "m_fFlags", sizeof(std::uint32_t));
        steamId_ = FindField(scope, "CBasePlayerController", "m_steamID", sizeof(std::uint64_t));
        name_ = FindField(scope, "CBasePlayerController", "m_iszPlayerName", 128);
        hltv_ = FindField(scope, "CBasePlayerController", "m_bIsHLTV", sizeof(bool));
        pawn_ = FindField(scope, "CBasePlayerController", "m_hPawn", sizeof(CEntityHandle));
        playerPawn_ = FindField(scope, "CCSPlayerController", "m_hPlayerPawn", sizeof(CEntityHandle));

        const auto path = std::string(ismm->GetBaseDir()) + "/addons/botmod/gamedata.ini";
        std::ifstream data(path);
        if (!data)
            throw std::runtime_error("Cannot open addons/botmod/gamedata.ini");
        std::string line, signature;
        while (std::getline(data, line)) {
            if (line.empty() || line.front() == '#')
                continue;
            const auto separator = line.find('=');
            if (separator == std::string::npos)
                continue;
            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);
            if (key == kPlatform)
                signature = value;
            else {
                int* offset = nullptr;
                if (key == "entity_system_offset") offset = &entitySystemOffset_;
                else if (key == "client_list_offset") offset = &clientLayout_.clients;
                else if (key == "client_slot_offset") offset = &clientLayout_.slot;
                else if (key == "client_server_offset") offset = &clientLayout_.server;
                else if (key == "client_channel_offset") offset = &clientLayout_.channel;
                else if (key == "client_connection_offset") offset = &clientLayout_.connection;
                else if (key == "client_fake_offset") offset = &clientLayout_.fake;
                else if (key == "client_userid_offset") offset = &clientLayout_.userId;
                else if (key == "client_steamid_offset") offset = &clientLayout_.steamId;
                else if (key == "client_steamid_mirror_offset") offset = &clientLayout_.steamIdMirror;
                else if (key == "client_hltv_offset") offset = &clientLayout_.hltv;
                if (!offset) continue;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), *offset);
                if (parsed.ec != std::errc{} ||
                    value.find_first_not_of(" \t\r", parsed.ptr - value.data()) != std::string::npos)
                    throw std::runtime_error("Invalid gamedata offset: " + key);
            }
        }
        if (entitySystemOffset_ < 0 || entitySystemOffset_ > 1024 || entitySystemOffset_ % alignof(void*) != 0)
            throw std::runtime_error("Invalid or missing entity_system_offset");
        if (!clientLayout_.Valid())
            throw std::runtime_error("Invalid or missing engine client offsets; install the 1.1.0 gamedata.ini");
        auto* target = botmod::FindUniqueEnginePattern(botmod::ParsePattern(signature));
        // Publish the trampoline before enabling callbacks, including for late loads.
        auto hook = safetyhook::InlineHook::create(target, &Botmod::PackEntities,
                                                 safetyhook::InlineHook::StartDisabled);
        if (!hook)
            throw std::runtime_error("Could not install the PackEntities hook");
        packHook_ = std::move(*hook);
        if (!packHook_.enable())
            throw std::runtime_error("Could not enable the PackEntities hook");
        if (!EnsureUserInfoHook())
            throw std::runtime_error("Could not install player-info hooks");
        ConCommandCreation_t command;
        command.m_pszName = "botmod_status";
        command.m_pszHelpString = "Print Botmod hooks, native bot state, and published player identities.";
        command.m_nFlags = FCVAR_RELEASE;
        command.m_CBInfo = ConCommandCallbackInfo_t(&Botmod::Status);
        statusCommand_ = cvar_->RegisterConCommand(command);
        if (!statusCommand_.IsValidRef())
            throw std::runtime_error("Could not register botmod_status");
        ismm->AddListener(this, this);
        META_CONPRINTF("[Botmod] Loaded 1.1.1 (Metamod API 17). Engine player-info and snapshot overrides; schema flags=%d, SteamID=%d, name=%d, pawn=%d, playerPawn=%d. Run botmod_status to verify publication.\n",
                       flags_.offset, steamId_.offset, name_.offset, pawn_.offset, playerPawn_.offset);
        return true;
    } catch (const std::exception& exception) {
        if (cvar_ && statusCommand_.IsValidRef()) {
            cvar_->UnregisterConCommandCallbacks(statusCommand_);
            statusCommand_.InvalidateRef();
        }
        userInfoHook_.reset();
        packHook_.reset();
        resources_ = nullptr;
        std::snprintf(error, maxlen, "%s", exception.what());
        return false;
    }
}

bool Botmod::EnsureUserInfoHook()
{
    if (userInfoHook_) return true;
    auto* server = network_->GetIGameServer();
    // Startup can precede server creation. LevelInit retries, and the first
    // snapshot refreshes any player-info records created before installation.
    if (!server) return true;
    auto* target = SourceHook::GetOrigVfnPtrEntry(server, &CNetworkGameServerBase::UserInfoChanged, g_SHPtr);
    auto hook = safetyhook::InlineHook::create(target, &Botmod::UserInfoChanged,
                                              safetyhook::InlineHook::StartDisabled);
    if (!hook) return false;
    userInfoHook_ = std::move(*hook);
    if (!userInfoHook_.enable()) {
        userInfoHook_.reset();
        return false;
    }
    META_CONPRINTF("[Botmod] UserInfoChanged hook ready (engine fake-player flag and both SteamID fields).\n");
    return true;
}

void* Botmod::GetClient(CNetworkGameServerBase* server, int slot) const
{
    if (!server || slot < 0 || slot >= kMaxPlayers) return nullptr;
    const auto* clients = reinterpret_cast<const CUtlVector<void*>*>(
        reinterpret_cast<const unsigned char*>(server) + clientLayout_.clients);
    if (clients->Count() < 0 || clients->Count() > 256 || slot >= clients->Count() || !clients->Base())
        return nullptr;
    auto* client = clients->Element(slot);
    if (!client || botmod::ReadClient<int>(client, clientLayout_.slot) != slot ||
        botmod::ReadClient<void*>(client, clientLayout_.server) != server ||
        botmod::ReadClient<std::uint8_t>(client, clientLayout_.fake) > 1 ||
        botmod::ReadClient<std::uint8_t>(client, clientLayout_.hltv) > 1)
        return nullptr;
    return client;
}

std::uint64_t Botmod::DisplayId(int slot, CEntityInstance* controller) const
{
    const auto nativeId = controller ? At<std::uint64_t>(controller, steamId_) : 0;
    const auto candidate = nativeId ? nativeId : botmod::kDisplayIdBase + slot + 1;
    auto* system = EntitySystem();
    // Preserve custom identities, but never publish a duplicate scoreboard ID.
    for (int peerSlot = 0; peerSlot < kMaxPlayers; ++peerSlot) {
        auto* peer = GetEntity(system, peerSlot + 1);
        if (peerSlot != slot && peer && std::strcmp(peer->GetClassname(), "cs_player_controller") == 0 &&
            At<std::uint64_t>(peer, steamId_) == candidate)
            return 0;
    }
    return candidate;
}

void Botmod::UserInfoChanged(CNetworkGameServerBase* server, CPlayerSlot playerSlot)
{
    auto& self = g_Botmod;
    std::lock_guard lock(self.packingMutex_);
    ++self.userInfoCalls_;
    ++self.userInfoDepth_;
    struct DepthOnExit {
        ~DepthOnExit() { --g_Botmod.userInfoDepth_; }
    } depth;
    const int slot = playerSlot.Get();
    auto* client = self.GetClient(server, slot);
    const auto& layout = self.clientLayout_;
    std::optional<botmod::ClientDisplayOverride> display;
    std::uint16_t userId = 0;
    if (client && botmod::ReadClient<std::uint8_t>(client, layout.fake) == 1 &&
        !botmod::ReadClient<std::uint8_t>(client, layout.hltv) &&
        !botmod::ReadClient<void*>(client, layout.channel)) {
        auto* controller = GetEntity(self.EntitySystem(), slot + 1);
        if (controller && std::strcmp(controller->GetClassname(), "cs_player_controller") != 0)
            controller = nullptr;
        userId = botmod::ReadClient<std::uint16_t>(client, layout.userId);
        const auto displayId = self.DisplayId(slot, controller);
        if (displayId) display.emplace(client, layout, displayId);
    }
    // Original code serializes CMsgPlayerInfo and marks the userinfo table dirty.
    // Hook the function itself, so internal (non-virtual) engine calls also pass
    // here, including SetFakeClientConVar(name) from VStrikeIdentity.
    self.userInfoHook_.call<void>(server, playerSlot);
    if (display) {
        ++self.userInfoOverrides_;
        if (self.GetClient(server, slot) != client ||
            botmod::ReadClient<std::uint16_t>(client, layout.userId) != userId)
            display->Abandon();
        display.reset();
        if (!self.reportedUserInfo_) {
            const auto published = self.PublishedInfo(slot);
            META_CONPRINTF("[Botmod] Player-info override ran; published slot=%d fakeplayer=%s. Native state restored for VStrikeIdentity. Run botmod_status for details.\n",
                           slot, published ? (published->fake ? "true (FAILED)" : "false") : "unknown (unreadable userinfo)");
            self.reportedUserInfo_ = true;
        }
    }
}

void Botmod::RefreshUserInfo()
{
    auto* server = network_->GetIGameServer();
    if (!server) return;
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        auto* client = GetClient(server, slot);
        if (client && botmod::ReadClient<std::uint8_t>(client, clientLayout_.fake) == 1 &&
            !botmod::ReadClient<std::uint8_t>(client, clientLayout_.hltv))
            server->UserInfoChanged(CPlayerSlot(slot));
    }
}

std::optional<botmod::PlayerInfo> Botmod::PublishedInfo(int slot) const
{
    auto* table = tables_ ? tables_->FindTable("userinfo") : nullptr;
    if (!table) return std::nullopt;
    const auto key = std::to_string(slot);
    const int index = table->FindStringIndex(key.c_str());
    if (index < 0 || index >= table->GetNumStrings()) return std::nullopt;
    const auto* data = table->GetStringUserData(index);
    return data ? botmod::ReadPlayerInfo(data->m_pRawData, data->m_cbDataSize) : std::nullopt;
}

void Botmod::Status()
{
    auto& self = g_Botmod;
    std::lock_guard lock(self.packingMutex_);
    META_CONPRINTF("[Botmod] version=1.1.1 platform=%s API=17 pack_hook=%d userinfo_hook=%d pack_calls=%llu userinfo_calls=%llu overrides=%llu last_controllers=%d last_pawns=%d\n",
                   kPlatform, !!self.packHook_, !!self.userInfoHook_,
                   static_cast<unsigned long long>(self.packCalls_),
                   static_cast<unsigned long long>(self.userInfoCalls_),
                   static_cast<unsigned long long>(self.userInfoOverrides_), self.lastControllers_, self.lastPawns_);
    auto* server = self.network_->GetIGameServer();
    auto* system = self.EntitySystem();
    int bots = 0;
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        auto* controller = GetEntity(system, slot + 1);
        if (!controller || std::strcmp(controller->GetClassname(), "cs_player_controller") != 0 ||
            At<bool>(controller, self.hltv_) ||
            !(At<std::uint32_t>(controller, self.flags_) & botmod::kFakeClient)) continue;
        ++bots;
        auto* client = self.GetClient(server, slot);
        const auto info = self.PublishedInfo(slot);
        const auto expectedId = self.DisplayId(slot, controller);
        const bool matches = info && !info->fake && !info->hltv && expectedId &&
                             info->xuid == expectedId && info->steamId == expectedId;
        META_CONPRINTF("[Botmod] slot=%d name=\"%.*s\" native_fake=1 engine_client=%s engine_fake=%d channel=%d published_fake=%s identity=%s\n",
                       slot, self.name_.size, &At<char>(controller, self.name_), client ? "valid" : "INVALID",
                       client ? botmod::ReadClient<std::uint8_t>(client, self.clientLayout_.fake) : -1,
                       client ? !!botmod::ReadClient<void*>(client, self.clientLayout_.channel) : -1,
                       info ? (info->fake ? "true" : "false") : "UNKNOWN",
                       matches ? "MATCH" : (!expectedId ? "COLLISION" : "MISMATCH_OR_UNREADABLE"));
    }
    META_CONPRINTF("[Botmod] native_bots=%d. native_fake=1 is expected for VStrike; published_fake=false and identity=MATCH are expected for clients. Snapshot counts describe the latest pack, not a client UI check.\n", bots);
}

void Botmod::OnLevelInit(const char*, const char*, const char*, const char*, bool, bool)
{
    refreshUserInfo_ = true;
    if (!EnsureUserInfoHook())
        META_CONPRINTF("[Botmod] ERROR: Player-info hook unavailable during level initialization.\n");
}

void Botmod::MarkDisplayChanged(CEntityInstance* entity) const
{
    entity->NetworkStateChanged(NetworkStateChangedData{
        static_cast<uint32>(flags_.offset), static_cast<uint32>(steamId_.offset),
        static_cast<uint32>(name_.offset)});
}

void Botmod::PackEntities(void* server, void* snapshot, int count, void* clients, void* transmit)
{
    g_Botmod.PackPre();
    struct RestoreOnExit {
        ~RestoreOnExit() { g_Botmod.PackPost(); }
    } restore;
    g_Botmod.packHook_.call<void>(server, snapshot, count, clients, transmit);
}

void Botmod::OverridePawn(CEntityInstance* pawn)
{
    if (!pawn || !(At<std::uint32_t>(pawn, flags_) & botmod::kBot))
        return;
    // A pawn already captured through another handle has FL_BOT cleared.
    auto& snapshot = pawnSnapshots_[pawnCount_++];
    snapshot.entity = pawn;
    snapshot.handle = pawn->GetRefEHandle();
    snapshot.display.emplace(At<std::uint32_t>(pawn, flags_));
    pawn->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(flags_.offset)));
}

void Botmod::PackPre()
{
    packingMutex_.lock();
    if (packingDepth_.fetch_add(1) != 0)
        return;
    ++packCalls_;

    // Avoid sharing GameFrame/OnClientConnected hook managers with CSS. Its
    // deferred VStrike commands must keep ticking across unload and map change.
    if (!EnsureUserInfoHook()) {
        if (!reportedUserInfoFailure_) {
            META_CONPRINTF("[Botmod] ERROR: UserInfoChanged hook failed; BOT labels may remain.\n");
            reportedUserInfoFailure_ = true;
        }
    } else if (userInfoHook_ && refreshUserInfo_) {
        RefreshUserInfo();
        refreshUserInfo_ = false;
    }

    auto* system = EntitySystem();
    int changed = 0;
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        auto* controller = GetEntity(system, slot + 1);
        if (!controller || std::strcmp(controller->GetClassname(), "cs_player_controller") != 0 ||
            At<bool>(controller, hltv_))
            continue;
        auto& flags = At<std::uint32_t>(controller, flags_);
        if (!(flags & botmod::kFakeClient))
            continue;
        auto& steamId = At<std::uint64_t>(controller, steamId_);
        const auto displayId = DisplayId(slot, controller);
        if (!displayId) continue;
        auto& snapshot = snapshots_[slot];
        snapshot.entity = controller;
        snapshot.handle = controller->GetRefEHandle();
        snapshot.display.emplace(flags, steamId, &At<char>(controller, name_), name_.size, displayId);
        MarkDisplayChanged(controller);
        OverridePawn(GetPawn(system, controller, pawn_));
        OverridePawn(GetPawn(system, controller, playerPawn_));
        ++changed;
    }
    lastControllers_ = changed;
    lastPawns_ = static_cast<int>(pawnCount_);
    if (changed && !reported_) {
        META_CONPRINTF("[Botmod] Applied display overrides to %d bot(s); native state restores after packing.\n", changed);
        reported_ = true;
    }
    if (pawnCount_ && !reportedPawns_) {
        META_CONPRINTF("[Botmod] Applied FL_BOT display overrides to %d pawn(s); native flags restore after packing.\n",
                       static_cast<int>(pawnCount_));
        reportedPawns_ = true;
    }
}

void Botmod::Restore()
{
    auto* system = EntitySystem();
    // Validate pawns independently: death/respawn can replace a pawn without
    // replacing its controller, and a controller can disappear first.
    for (std::size_t i = 0; i < pawnCount_; ++i) {
        auto& snapshot = pawnSnapshots_[i];
        auto* current = GetEntity(system, snapshot.handle.GetEntryIndex());
        if (current != snapshot.entity || !current || current->GetRefEHandle() != snapshot.handle)
            snapshot.display->Abandon();
        snapshot.display.reset();
        snapshot.entity = nullptr;
    }
    pawnCount_ = 0;
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        auto& snapshot = snapshots_[slot];
        if (!snapshot.display)
            continue;
        auto* current = GetEntity(system, slot + 1);
        if (current != snapshot.entity || !current || current->GetRefEHandle() != snapshot.handle)
            snapshot.display->Abandon();
        // Deliberately do not mark the native restoration as a network change.
        snapshot.display.reset();
        snapshot.entity = nullptr;
    }
}

void Botmod::PackPost()
{
    if (packingDepth_.fetch_sub(1) == 1)
        Restore();
    packingMutex_.unlock();
}

bool Botmod::Unload(char* error, size_t maxlen)
{
    if (packingDepth_.load() != 0 || userInfoDepth_.load() != 0) {
        std::snprintf(error, maxlen, "A network snapshot is in progress; retry unloading between frames");
        return false;
    }
    if (!packHook_.disable()) {
        std::snprintf(error, maxlen, "Could not disable the PackEntities hook; plugin remains loaded");
        return false;
    }
    if (userInfoHook_ && !userInfoHook_.disable()) {
        (void)packHook_.enable();
        std::snprintf(error, maxlen, "Could not disable the player-info hook; plugin remains loaded");
        return false;
    }
    userInfoHook_.reset();
    packHook_.reset();
    if (statusCommand_.IsValidRef()) {
        cvar_->UnregisterConCommandCallbacks(statusCommand_);
        statusCommand_.InvalidateRef();
    }
    Restore();
    // Force the original values back into the next outgoing snapshot.
    auto* system = EntitySystem();
    for (int index = 1; index <= kMaxPlayers; ++index) {
        auto* controller = GetEntity(system, index);
        if (controller && std::strcmp(controller->GetClassname(), "cs_player_controller") == 0 &&
            !At<bool>(controller, hltv_) &&
            (At<std::uint32_t>(controller, flags_) & botmod::kFakeClient)) {
            MarkDisplayChanged(controller);
            for (const auto field : {pawn_, playerPawn_}) {
                auto* pawn = GetPawn(system, controller, field);
                if (pawn)
                    pawn->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(flags_.offset)));
            }
        }
    }
    RefreshUserInfo();
    resources_ = nullptr;
    reported_ = false;
    reportedPawns_ = false;
    reportedUserInfo_ = false;
    reportedUserInfoFailure_ = false;
    refreshUserInfo_ = true;
    META_CONPRINTF("[Botmod] Unloaded; native bot display restored.\n");
    return true;
}

} // namespace

PLUGIN_EXPOSE(Botmod, g_Botmod);
