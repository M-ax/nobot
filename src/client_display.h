#pragma once

#include <cstdint>
#include <cstring>

namespace botmod {

// Engine client fields are not in the entity schema. Values come from gamedata.
struct ClientLayout {
    int clients = -1;
    int slot = -1;
    int server = -1;
    int channel = -1;
    int connection = -1;
    int fake = -1;
    int userId = -1;
    int steamId = -1;
    int steamIdMirror = -1;
    int hltv = -1;
    bool Valid() const;
};

template<typename T>
T ReadClient(const void* client, int offset)
{
    T value;
    std::memcpy(&value, static_cast<const unsigned char*>(client) + offset, sizeof(value));
    return value;
}

// Use Bot-Hider's engine identity while UserInfoChanged serializes player info.
// Keep native state outside that call so VStrike's IsBot and fake-client naming
// (as well as quota/AI) do not require a managed-code compatibility patch.
class ClientDisplayOverride {
public:
    ClientDisplayOverride(void* client, const ClientLayout& layout, std::uint64_t displayId);
    ~ClientDisplayOverride();
    ClientDisplayOverride(const ClientDisplayOverride&) = delete;
    ClientDisplayOverride& operator=(const ClientDisplayOverride&) = delete;
    void Abandon() { client_ = nullptr; }

private:
    void* client_;
    ClientLayout layout_;
    std::uint8_t connection_;
    std::uint8_t fake_;
    std::uint64_t steamId_;
    std::uint64_t steamIdMirror_;
};

} // namespace botmod
