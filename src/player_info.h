#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace botmod {
struct PlayerInfo {
    std::uint64_t xuid = 0;
    std::uint64_t steamId = 0;
    bool fake = false;
    bool hltv = false;
};

// Read only the identity fields in networkbasetypes.proto's CMsgPlayerInfo.
// Reject malformed/truncated data instead of reporting a false success.
std::optional<PlayerInfo> ReadPlayerInfo(const void* data, std::size_t size);
}
