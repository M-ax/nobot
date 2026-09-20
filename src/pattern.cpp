#include "pattern.h"

#include <sstream>
#include <stdexcept>
#include <string>

namespace botmod {

std::vector<int> ParsePattern(std::string_view text)
{
    std::istringstream input{std::string(text)};
    std::vector<int> result;
    std::string token;
    while (input >> token) {
        if (token == "?" || token == "??") {
            result.push_back(-1);
            continue;
        }
        if (token.size() != 2 || token.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            throw std::runtime_error("Invalid byte in PackEntities signature");
        const auto nibble = [](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        };
        result.push_back(nibble(token[0]) * 16 + nibble(token[1]));
    }
    if (result.size() < 8)
        throw std::runtime_error("PackEntities signature is too short");
    return result;
}

std::vector<const std::uint8_t*> FindPattern(const std::uint8_t* bytes, std::size_t size,
                                          const std::vector<int>& pattern)
{
    std::vector<const std::uint8_t*> matches;
    if (!bytes || pattern.empty() || pattern.size() > size)
        return matches;
    for (std::size_t i = 0; i <= size - pattern.size(); ++i) {
        std::size_t j = 0;
        for (; j < pattern.size(); ++j)
            if (pattern[j] >= 0 && bytes[i + j] != pattern[j])
                break;
        if (j == pattern.size())
            matches.push_back(bytes + i);
    }
    return matches;
}

} // namespace botmod
