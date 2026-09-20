#include "player_info.h"

namespace botmod {
std::optional<PlayerInfo> ReadPlayerInfo(const void* data, std::size_t size)
{
    if (!data || !size || size > 65536) return std::nullopt;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t pos = 0;
    auto varint = [&](std::uint64_t& value) {
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            if (pos == size) return false;
            const auto byte = bytes[pos++];
            if (shift == 63 && byte > 1) return false;
            value |= std::uint64_t(byte & 127) << shift;
            if (!(byte & 128)) return true;
        }
        return false;
    };
    PlayerInfo info;
    while (pos < size) {
        std::uint64_t key = 0, value = 0;
        if (!varint(key) || (key >> 3) == 0 || (key >> 3) > 0x1fffffff)
            return std::nullopt;
        const auto field = key >> 3;
        const auto wire = key & 7;
        if (((field == 2 || field == 4) && wire != 1) ||
            ((field == 5 || field == 6) && wire != 0)) return std::nullopt;
        switch (wire) {
        case 0:
            if (!varint(value)) return std::nullopt;
            if (field == 5) info.fake = value != 0;
            if (field == 6) info.hltv = value != 0;
            break;
        case 1:
            if (size - pos < 8) return std::nullopt;
            for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(bytes[pos++]) << (8 * i);
            if (field == 2) info.xuid = value;
            if (field == 4) info.steamId = value;
            break;
        case 2:
            if (!varint(value) || value > size - pos) return std::nullopt;
            pos += static_cast<std::size_t>(value);
            break;
        case 5:
            if (size - pos < 4) return std::nullopt;
            pos += 4;
            break;
        default:
            return std::nullopt;
        }
    }
    return info;
}
}
