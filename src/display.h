#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace botmod {

constexpr std::uint32_t kFakeClient = 1u << 8;
// A display-only ID, never used for authentication or written to engine clients.
constexpr std::uint64_t kDisplayIdBase = 0x01100001F0000000ULL;

std::string_view WithoutBotPrefix(std::string_view name);

// Applies only inside PackEntities. Destruction restores every byte we changed.
class DisplayOverride {
public:
    DisplayOverride(std::uint32_t& flags, std::uint64_t& steamId,
                    char* name, std::size_t nameCapacity, std::uint64_t displayId);
    ~DisplayOverride();
    DisplayOverride(const DisplayOverride&) = delete;
    DisplayOverride& operator=(const DisplayOverride&) = delete;
    bool NameChanged() const { return nameChanged_; }
    void Abandon() { active_ = false; }

private:
    std::uint32_t& flags_;
    std::uint64_t& steamId_;
    char* name_;
    std::uint32_t oldFlags_;
    std::uint64_t oldSteamId_;
    char oldName_[128]{};
    std::size_t nameCapacity_ = 0;
    bool nameChanged_ = false;
    bool active_ = false;
};

} // namespace botmod
