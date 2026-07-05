#include "executable_path.hpp"

#include <string>
#include <vector>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#elif defined(__APPLE__)
#    include <mach-o/dyld.h>
#else
#    include <unistd.h>
#endif

namespace slayerlog
{

std::filesystem::path executable_path()
{
#if defined(_WIN32)
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return {};
        }
        if (length < buffer.size())
        {
            return std::filesystem::path(std::wstring(buffer.data(), length));
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        return {};
    }
    std::error_code error_code;
    auto canonical = std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()), error_code);
    return error_code ? std::filesystem::path(buffer.data()) : canonical;
#else
    std::error_code error_code;
    auto resolved = std::filesystem::read_symlink("/proc/self/exe", error_code);
    return error_code ? std::filesystem::path {} : resolved;
#endif
}

} // namespace slayerlog
