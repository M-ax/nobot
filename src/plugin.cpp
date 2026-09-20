#include "display.h"
#include "pattern.h"

#include <safetyhook.hpp>
#include <ISmmPlugin.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <schemasystem/schemasystem.h>

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

class Botmod final : public ISmmPlugin {
public:
    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;
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
    const char* GetVersion() override { return "1.0.2"; }
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

    void* resources_ = nullptr;
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
    try {
        auto factory = ismm->GetEngineFactory();
        resources_ = factory(GAMERESOURCESERVICESERVER_INTERFACE_VERSION, nullptr);
        auto* schema = static_cast<CSchemaSystem*>(factory(SCHEMASYSTEM_INTERFACE_VERSION, nullptr));
        if (!resources_ || !schema)
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
            else if (key == "entity_system_offset") {
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), entitySystemOffset_);
                if (parsed.ec != std::errc{} ||
                    value.find_first_not_of(" \t\r", parsed.ptr - value.data()) != std::string::npos)
                    throw std::runtime_error("Invalid entity_system_offset");
            }
        }
        if (entitySystemOffset_ < 0 || entitySystemOffset_ > 1024 || entitySystemOffset_ % alignof(void*) != 0)
            throw std::runtime_error("Invalid or missing entity_system_offset");
        auto* target = botmod::FindUniqueEnginePattern(botmod::ParsePattern(signature));
        // Publish the trampoline before enabling callbacks, including for late loads.
        auto hook = safetyhook::InlineHook::create(target, &Botmod::PackEntities,
                                                 safetyhook::InlineHook::StartDisabled);
        if (!hook)
            throw std::runtime_error("Could not install the PackEntities hook");
        packHook_ = std::move(*hook);
        if (!packHook_.enable())
            throw std::runtime_error("Could not enable the PackEntities hook");
        META_CONPRINTF("[Botmod] Loaded 1.0.2 (Metamod API 17). Snapshot hook ready; schema flags=%d, SteamID=%d, name=%d, pawn=%d, playerPawn=%d.\n",
                       flags_.offset, steamId_.offset, name_.offset, pawn_.offset, playerPawn_.offset);
        return true;
    } catch (const std::exception& exception) {
        packHook_.reset();
        resources_ = nullptr;
        std::snprintf(error, maxlen, "%s", exception.what());
        return false;
    }
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
        // Real Steam identities assigned by another plugin are left alone.
        auto displayId = steamId ? steamId : botmod::kDisplayIdBase + slot + 1;
        // Do not let a custom identity collide with a generated one.
        bool collision = false;
        for (int other = 0; other < kMaxPlayers; ++other) {
            auto* peer = GetEntity(system, other + 1);
            if (other != slot && peer && std::strcmp(peer->GetClassname(), "cs_player_controller") == 0 &&
                At<std::uint64_t>(peer, steamId_) == displayId) {
                collision = true;
                break;
            }
        }
        if (collision)
            continue;
        auto& snapshot = snapshots_[slot];
        snapshot.entity = controller;
        snapshot.handle = controller->GetRefEHandle();
        snapshot.display.emplace(flags, steamId, &At<char>(controller, name_), name_.size, displayId);
        MarkDisplayChanged(controller);
        OverridePawn(GetPawn(system, controller, pawn_));
        OverridePawn(GetPawn(system, controller, playerPawn_));
        ++changed;
    }
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
    if (packingDepth_.load() != 0) {
        std::snprintf(error, maxlen, "A network snapshot is in progress; retry unloading between frames");
        return false;
    }
    if (!packHook_.disable()) {
        std::snprintf(error, maxlen, "Could not disable the PackEntities hook; plugin remains loaded");
        return false;
    }
    packHook_.reset();
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
    resources_ = nullptr;
    reported_ = false;
    reportedPawns_ = false;
    META_CONPRINTF("[Botmod] Unloaded; native bot display restored.\n");
    return true;
}

} // namespace

PLUGIN_EXPOSE(Botmod, g_Botmod);
