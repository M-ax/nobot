#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace botmod {
std::vector<int> ParsePattern(std::string_view text);
std::vector<const std::uint8_t*> FindPattern(const std::uint8_t* bytes, std::size_t size,
                                          const std::vector<int>& pattern);
void* FindUniqueEnginePattern(const std::vector<int>& pattern);
} // namespace botmod

