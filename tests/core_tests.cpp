#include "display.h"
#include "client_display.h"
#include "pattern.h"
#include "player_info.h"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
int failures = 0;
void Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main()
{
    using namespace botmod;
    // Real protobuf field numbers, including unknown fields and embedded names.
    const unsigned char playerInfo[] = {10, 3, 'B', 'o', 'b', 17, 1, 2, 3, 4, 5, 6, 7, 8,
        33, 8, 7, 6, 5, 4, 3, 2, 1, 40, 0, 48, 1, 120, 42, 125, 0, 0, 0, 0};
    auto published = ReadPlayerInfo(playerInfo, sizeof(playerInfo));
    Check(published && published->xuid == 0x0807060504030201ULL &&
          published->steamId == 0x0102030405060708ULL && !published->fake && published->hltv,
          "published identity decoded with unknown fields skipped");
    const unsigned char nativeInfo[] = {40, 1};
    Check(ReadPlayerInfo(nativeInfo, sizeof(nativeInfo))->fake, "native fake-player record detected");
    const unsigned char repeatedInfo[] = {40, 1, 40, 0};
    Check(!ReadPlayerInfo(repeatedInfo, sizeof(repeatedInfo))->fake, "last protobuf value wins");
    const unsigned char badLength[] = {10, 127, 'x'};
    const unsigned char badWire[] = {42, 0};
    const unsigned char badVarint[] = {40, 128, 128, 128, 128, 128, 128, 128, 128, 128, 2};
    Check(!ReadPlayerInfo(nullptr, 0) && !ReadPlayerInfo(badLength, sizeof(badLength)) &&
          !ReadPlayerInfo(badWire, sizeof(badWire)) && !ReadPlayerInfo(badVarint, sizeof(badVarint)) &&
          !ReadPlayerInfo(playerInfo, 12), "unreadable protobuf is not reported as a successful override");
    Check(WithoutBotPrefix("[BOT] Bob") == "Bob", "bracketed prefix");
    Check(WithoutBotPrefix("BOT Bob") == "Bob", "native literal prefix");
    Check(WithoutBotPrefix("[BOT] [BOT] BOT Bob") == "Bob", "repeated prefixes");
    Check(WithoutBotPrefix("[BOT] \t\xE5\xB0\x8F\xE6\x98\x8E") == "\xE5\xB0\x8F\xE6\x98\x8E", "UTF-8 is preserved");
    Check(WithoutBotPrefix("BOTany") == "BOTany", "ordinary names preserved");
    Check(WithoutBotPrefix("Alice [BOT]") == "Alice [BOT]", "suffix preserved");
    Check(WithoutBotPrefix("[BOT] ") == "[BOT] ", "empty result rejected");

    // Reproduce the engine's unaligned SteamID layout. Entity-only tests used
    // to pass while CMsgPlayerInfo still advertised fakeplayer=true.
    const ClientLayout layout{584, 72, 80, 88, 96, 160, 168, 171, 179, 322};
    Check(layout.Valid(), "current engine client layout accepted");
    Check(!ClientLayout{}.Valid(), "missing gamedata rejected");
    auto overlapping = layout;
    overlapping.steamIdMirror = layout.steamId + 1;
    Check(!overlapping.Valid(), "overlapping client fields rejected");
    std::array<unsigned char, 384> client;
    client.fill(0x5a);
    client[layout.fake] = 1;
    client[layout.connection] = 0x28; // Preserve unrelated connection bits.
    const auto nativeClient = client;
    {
        ClientDisplayOverride info(client.data(), layout, kDisplayIdBase + 2);
        Check(client[layout.fake] == 0 && client[layout.connection] == 0x21,
              "player-info serialization sees both fake-client markers cleared");
        Check(ReadClient<std::uint64_t>(client.data(), layout.steamId) == kDisplayIdBase + 2 &&
              ReadClient<std::uint64_t>(client.data(), layout.steamIdMirror) == kDisplayIdBase + 2,
              "both unaligned engine identities match the controller display ID");
        {
            ClientDisplayOverride nested(client.data(), layout, kDisplayIdBase + 3);
        }
        Check(client[layout.fake] == 0 &&
              ReadClient<std::uint64_t>(client.data(), layout.steamId) == kDisplayIdBase + 2,
              "nested userinfo restores the outer identity");
    }
    Check(client == nativeClient, "engine identity restored byte-for-byte before VStrike renames");
    try {
        ClientDisplayOverride info(client.data(), layout, kDisplayIdBase + 2);
        throw std::runtime_error("userinfo failed");
    } catch (const std::exception&) {}
    Check(client == nativeClient, "engine identity restored on exceptional exit");
    {
        ClientDisplayOverride info(client.data(), layout, kDisplayIdBase + 2);
        info.Abandon();
        client.fill(0x42);
    }
    Check(client[layout.fake] == 0x42 && client[layout.steamId] == 0x42,
          "reused client is not overwritten during restoration");

    std::uint32_t flags = kFakeClient | 0x80200001u;
    const auto originalFlags = flags;
    std::uint64_t steamId = 0;
    char name[128];
    std::memset(name, 0x5a, sizeof(name));
    std::memcpy(name, "[BOT] Bob", 10);
    std::array<char, 128> originalName;
    std::memcpy(originalName.data(), name, sizeof(name));
    {
        DisplayOverride display(flags, steamId, name, sizeof(name), kDisplayIdBase + 1);
        Check(flags == (originalFlags & ~kFakeClient), "only fake-client flag removed");
        Check(steamId == kDisplayIdBase + 1, "nonzero distinct display identity");
        Check(std::strcmp(name, "Bob") == 0, "name changed during snapshot");
        Check(display.NameChanged(), "name dirty state");
        {
            DisplayOverride nested(flags, steamId, name, sizeof(name), kDisplayIdBase + 2);
            Check(steamId == kDisplayIdBase + 1, "nested snapshot preserves outer identity");
        }
        Check(!(flags & kFakeClient), "nested restoration preserves outer display");
    }
    Check(flags == originalFlags && steamId == 0, "native bot state restored");
    Check(std::memcmp(name, originalName.data(), sizeof(name)) == 0, "full name buffer restored byte for byte");

    // Regression: controller-only snapshots left the pawn's FL_BOT visible.
    std::uint32_t pawnFlags = kBot | 0x80200001u;
    const auto originalPawnFlags = pawnFlags;
    {
        DisplayOverride controller(flags, steamId, name, sizeof(name), kDisplayIdBase + 1);
        PawnDisplayOverride pawn(pawnFlags);
        Check(!(flags & kFakeClient) && !(pawnFlags & kBot), "both bot markers cleared in the same snapshot");
        Check(pawnFlags == (originalPawnFlags & ~kBot), "pawn movement flags preserved");
        {
            PawnDisplayOverride nested(pawnFlags);
        }
        Check(!(pawnFlags & kBot), "nested pawn restoration preserves outer override");
    }
    Check(flags == originalFlags && pawnFlags == originalPawnFlags, "controller and pawn native state restored");
    {
        PawnDisplayOverride deleted(pawnFlags);
        deleted.Abandon();
        pawnFlags = 0x42;
    }
    Check(pawnFlags == 0x42, "reused pawn is not restored");
    pawnFlags = originalPawnFlags;
    try {
        PawnDisplayOverride pawn(pawnFlags);
        throw std::runtime_error("packing failed");
    } catch (const std::exception&) {}
    Check(pawnFlags == originalPawnFlags, "pawn native state restored on exceptional exit");

    flags = 1;
    steamId = 76561198000000001ULL;
    {
        DisplayOverride human(flags, steamId, name, sizeof(name), kDisplayIdBase + 1);
        Check(flags == 1 && steamId == 76561198000000001ULL, "human identity untouched");
        Check(std::memcmp(name, originalName.data(), sizeof(name)) == 0, "human name untouched");
    }
    flags = kFakeClient;
    {
        DisplayOverride deleted(flags, steamId, name, sizeof(name), kDisplayIdBase + 1);
        deleted.Abandon();
        flags = 42;
        steamId = 123;
    }
    Check(flags == 42 && steamId == 123, "reused slot is not restored");
    flags = kFakeClient;
    std::memset(name, 'x', sizeof(name));
    {
        DisplayOverride malformed(flags, steamId, name, sizeof(name), kDisplayIdBase + 1);
        Check(!malformed.NameChanged(), "unterminated name rejected");
    }

    const std::uint8_t bytes[] = {0x48, 0x89, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                0x48, 0x89, 0xff, 0x02, 0x03, 0x04, 0x05, 0x06};
    const auto pattern = ParsePattern("48 89 ? 02 03 04 05 06");
    Check(FindPattern(bytes, sizeof(bytes), pattern).size() == 2, "ambiguous signatures detected");
    Check(FindPattern(bytes, 7, pattern).empty(), "pattern longer than buffer");
    Check(FindPattern(bytes, 8, pattern).size() == 1, "boundary match");
    Check(FindPattern(nullptr, 0, pattern).empty(), "null buffer");
    bool rejected = false;
    try { ParsePattern("48 89 GG 02 03 04 05 06"); }
    catch (const std::exception&) { rejected = true; }
    Check(rejected, "malformed signature rejected");
    if (failures == 0)
        std::cout << "All display restoration, identity, name, and signature tests passed.\n";
    return failures ? 1 : 0;
}
