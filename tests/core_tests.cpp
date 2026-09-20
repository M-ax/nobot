#include "display.h"
#include "pattern.h"

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
    Check(WithoutBotPrefix("[BOT] Bob") == "Bob", "bracketed prefix");
    Check(WithoutBotPrefix("BOT Bob") == "Bob", "native literal prefix");
    Check(WithoutBotPrefix("[BOT] [BOT] BOT Bob") == "Bob", "repeated prefixes");
    Check(WithoutBotPrefix("[BOT] \t\xE5\xB0\x8F\xE6\x98\x8E") == "\xE5\xB0\x8F\xE6\x98\x8E", "UTF-8 is preserved");
    Check(WithoutBotPrefix("BOTany") == "BOTany", "ordinary names preserved");
    Check(WithoutBotPrefix("Alice [BOT]") == "Alice [BOT]", "suffix preserved");
    Check(WithoutBotPrefix("[BOT] ") == "[BOT] ", "empty result rejected");

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
