#include "display.h"

#include <cstring>

namespace botmod {

std::string_view WithoutBotPrefix(std::string_view name)
{
    const auto original = name;
    for (;;) {
        if (name.substr(0, 5) == "[BOT]") {
            name.remove_prefix(5);
        } else if (name.substr(0, 4) == "BOT ") {
            name.remove_prefix(4);
        } else {
            break;
        }
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
            name.remove_prefix(1);
    }
    // Never replace a bot's name with an empty string.
    return name.empty() ? original : name;
}

DisplayOverride::DisplayOverride(std::uint32_t& flags, std::uint64_t& steamId,
                                 char* name, std::size_t capacity, std::uint64_t displayId)
    : flags_(flags), steamId_(steamId), name_(name), oldFlags_(flags), oldSteamId_(steamId)
{
    if (!(flags & kFakeClient))
        return;
    active_ = true;
    flags_ &= ~kFakeClient;
    steamId_ = displayId;
    if (!name || capacity == 0 || capacity > sizeof(oldName_))
        return;
    const auto* end = static_cast<const char*>(std::memchr(name, '\0', capacity));
    if (!end)
        return;
    const std::string_view original(name, end - name);
    const auto clean = WithoutBotPrefix(original);
    if (clean == original)
        return;
    nameCapacity_ = capacity;
    std::memcpy(oldName_, name, capacity);
    std::memmove(name, clean.data(), clean.size());
    name[clean.size()] = '\0';
    nameChanged_ = true;
}

DisplayOverride::~DisplayOverride()
{
    if (!active_)
        return;
    flags_ = oldFlags_;
    steamId_ = oldSteamId_;
    if (nameChanged_)
        std::memcpy(name_, oldName_, nameCapacity_);
}

} // namespace botmod
