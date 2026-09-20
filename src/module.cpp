#include "pattern.h"

#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <link.h>
#endif

namespace botmod {

namespace {
std::vector<std::uint8_t> ReadModule(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read the loaded engine module for signature lookup");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void SearchSection(const std::vector<std::uint8_t>& image, std::size_t offset, std::size_t size,
                   const std::uint8_t* runtime, const std::vector<int>& pattern,
                   std::vector<const std::uint8_t*>& matches)
{
    if (offset > image.size() || size > image.size() - offset)
        throw std::runtime_error("Invalid engine executable section");
    const auto* section = image.data() + offset;
    for (auto* found : FindPattern(section, size, pattern))
        matches.push_back(runtime + (found - section));
}
} // namespace

void* FindUniqueEnginePattern(const std::vector<int>& pattern)
{
    std::vector<const std::uint8_t*> matches;
#ifdef _WIN32
    auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleA("engine2.dll"));
    if (!base)
        throw std::runtime_error("engine2.dll is not loaded");
    wchar_t filename[32768];
    const auto length = GetModuleFileNameW(reinterpret_cast<HMODULE>(const_cast<std::uint8_t*>(base)),
                                         filename, static_cast<DWORD>(std::size(filename)));
    if (!length || length == std::size(filename))
        throw std::runtime_error("Cannot resolve the loaded engine path");
    const auto image = ReadModule(std::filesystem::path(filename));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;
        // Other plugins and previously removed KHook detours may have patched
        // the entry point in memory. Scan the original file and translate RVAs.
        SearchSection(image, sections[i].PointerToRawData,
                      (std::min)(sections[i].SizeOfRawData, sections[i].Misc.VirtualSize),
                      base + sections[i].VirtualAddress, pattern, matches);
    }
#else
    struct Search {
        const std::vector<int>& pattern;
        std::vector<const std::uint8_t*>& matches;
        std::string error;
    } search{pattern, matches};
    dl_iterate_phdr([](dl_phdr_info* info, std::size_t, void* opaque) {
        const char* filename = std::strrchr(info->dlpi_name, '/');
        filename = filename ? filename + 1 : info->dlpi_name;
        if (std::strcmp(filename, "libengine2.so") != 0)
            return 0;
        auto& search = *static_cast<Search*>(opaque);
        try {
            const auto image = ReadModule(info->dlpi_name);
            for (unsigned int i = 0; i < info->dlpi_phnum; ++i) {
                const auto& segment = info->dlpi_phdr[i];
                if (segment.p_type != PT_LOAD || !(segment.p_flags & PF_X) || !(segment.p_flags & PF_R))
                    continue;
                SearchSection(image, segment.p_offset, segment.p_filesz,
                              reinterpret_cast<const std::uint8_t*>(info->dlpi_addr + segment.p_vaddr),
                              search.pattern, search.matches);
            }
        } catch (const std::exception& exception) {
            search.error = exception.what();
        }
        return 1;
    }, &search);
    if (!search.error.empty())
        throw std::runtime_error(search.error);
#endif
    if (matches.size() != 1)
        throw std::runtime_error("PackEntities signature must match exactly once; update addons/botmod/gamedata.ini");
    return const_cast<std::uint8_t*>(matches.front());
}

} // namespace botmod
