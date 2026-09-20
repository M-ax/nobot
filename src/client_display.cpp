#include "client_display.h"

#include <array>
#include <utility>

namespace botmod {

bool ClientLayout::Valid() const
{
    if (clients < 0 || clients > 4096 || clients % alignof(void*) != 0)
        return false;
    const std::array<std::pair<int, int>, 9> fields{{
        {slot, 4}, {server, 8}, {channel, 8}, {connection, 1}, {fake, 1},
        {userId, 2}, {steamId, 8}, {steamIdMirror, 8}, {hltv, 1}}};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto [offset, size] = fields[i];
        if (offset < 8 || offset > 4096 - size)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (offset < fields[j].first + fields[j].second && fields[j].first < offset + size)
                return false;
    }
    return true;
}

namespace {
template<typename T>
void WriteClient(void* client, int offset, T value)
{
    std::memcpy(static_cast<unsigned char*>(client) + offset, &value, sizeof(value));
}
}

ClientDisplayOverride::ClientDisplayOverride(void* client, const ClientLayout& layout, std::uint64_t displayId)
    : client_(client), layout_(layout),
      connection_(ReadClient<std::uint8_t>(client, layout.connection)),
      fake_(ReadClient<std::uint8_t>(client, layout.fake)),
      steamId_(ReadClient<std::uint64_t>(client, layout.steamId)),
      steamIdMirror_(ReadClient<std::uint64_t>(client, layout.steamIdMirror))
{
    WriteClient(client, layout.connection, static_cast<std::uint8_t>((connection_ & ~0x08u) | 0x01u));
    WriteClient(client, layout.fake, std::uint8_t{0});
    WriteClient(client, layout.steamId, displayId);
    WriteClient(client, layout.steamIdMirror, displayId);
}

ClientDisplayOverride::~ClientDisplayOverride()
{
    if (!client_)
        return;
    WriteClient(client_, layout_.connection, connection_);
    WriteClient(client_, layout_.fake, fake_);
    WriteClient(client_, layout_.steamId, steamId_);
    WriteClient(client_, layout_.steamIdMirror, steamIdMirror_);
}

} // namespace botmod
