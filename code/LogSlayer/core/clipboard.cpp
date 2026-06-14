#include "clipboard.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <limits>
#    include <windows.h>
#else
#    include <cctype>
#    include <csignal>
#    include <fstream>
#    include <iterator>
#endif

namespace
{

#ifndef _WIN32

bool env_var_is_set(const char* name)
{
    const auto* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

bool is_ssh_session()
{
    return env_var_is_set("SSH_CONNECTION") || env_var_is_set("SSH_CLIENT") || env_var_is_set("SSH_TTY");
}

std::string read_file(const char* path)
{
    std::ifstream file(path);
    if (!file)
    {
        return {};
    }

    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string to_lower_ascii(std::string value)
{
    for (char& character : value)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return value;
}

bool contains_case_insensitive(const std::string& haystack, const char* needle)
{
    return to_lower_ascii(haystack).find(needle) != std::string::npos;
}

bool is_wsl()
{
    if (env_var_is_set("WSL_INTEROP") || env_var_is_set("WSL_DISTRO_NAME"))
    {
        return true;
    }

    const std::string os_release = read_file("/proc/sys/kernel/osrelease");
    if (contains_case_insensitive(os_release, "microsoft") || contains_case_insensitive(os_release, "wsl"))
    {
        return true;
    }

    const std::string version = read_file("/proc/version");
    return contains_case_insensitive(version, "microsoft") || contains_case_insensitive(version, "wsl");
}

bool write_text_to_command(const char* command, const std::string& text)
{
    const auto previous_sigpipe_handler = std::signal(SIGPIPE, SIG_IGN);

    auto* pipe = popen(command, "w");
    if (pipe == nullptr)
    {
        if (previous_sigpipe_handler != SIG_ERR)
        {
            std::signal(SIGPIPE, previous_sigpipe_handler);
        }
        return false;
    }

    const std::size_t bytes_written = std::fwrite(text.data(), 1, text.size(), pipe);
    const bool flushed              = std::fflush(pipe) == 0;
    const int status                = pclose(pipe);

    if (previous_sigpipe_handler != SIG_ERR)
    {
        std::signal(SIGPIPE, previous_sigpipe_handler);
    }

    return bytes_written == text.size() && flushed && status == 0;
}

std::string base64_encode(const std::string& text)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((text.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 3 <= text.size())
    {
        const unsigned int value = (static_cast<unsigned char>(text[index]) << 16) | (static_cast<unsigned char>(text[index + 1]) << 8) | static_cast<unsigned char>(text[index + 2]);
        encoded.push_back(alphabet[(value >> 18) & 0x3F]);
        encoded.push_back(alphabet[(value >> 12) & 0x3F]);
        encoded.push_back(alphabet[(value >> 6) & 0x3F]);
        encoded.push_back(alphabet[value & 0x3F]);
        index += 3;
    }

    const std::size_t remainder = text.size() - index;
    if (remainder == 1)
    {
        const unsigned int value = static_cast<unsigned char>(text[index]) << 16;
        encoded.push_back(alphabet[(value >> 18) & 0x3F]);
        encoded.push_back(alphabet[(value >> 12) & 0x3F]);
        encoded.push_back('=');
        encoded.push_back('=');
    }
    else if (remainder == 2)
    {
        const unsigned int value = (static_cast<unsigned char>(text[index]) << 16) | (static_cast<unsigned char>(text[index + 1]) << 8);
        encoded.push_back(alphabet[(value >> 18) & 0x3F]);
        encoded.push_back(alphabet[(value >> 12) & 0x3F]);
        encoded.push_back(alphabet[(value >> 6) & 0x3F]);
        encoded.push_back('=');
    }

    return encoded;
}

std::string build_osc52_sequence(const std::string& text)
{
    const std::string payload = "52;c;" + base64_encode(text) + '\a';

    if (env_var_is_set("TMUX"))
    {
        return "\x1bPtmux;\x1b]" + payload + "\x1b\\";
    }

    const auto* term = std::getenv("TERM");
    if (term != nullptr && std::string(term).rfind("screen", 0) == 0)
    {
        return "\x1bP\x1b]" + payload + "\x1b\\";
    }

    return "\x1b]" + payload;
}

bool write_text_to_terminal_clipboard(const std::string& text)
{
    auto* terminal = std::fopen("/dev/tty", "w");
    if (terminal == nullptr)
    {
        return false;
    }

    const auto sequence      = build_osc52_sequence(text);
    const auto bytes_written = std::fwrite(sequence.data(), 1, sequence.size(), terminal);
    const bool flushed       = std::fflush(terminal) == 0;
    std::fclose(terminal);

    return bytes_written == sequence.size() && flushed;
}

bool copy_with_local_clipboard_tools(const std::string& text)
{
#    ifdef __APPLE__
    if (write_text_to_command("pbcopy 2>/dev/null", text))
    {
        return true;
    }
#    endif

    if (write_text_to_command("wl-copy --type text/plain;charset=utf-8 2>/dev/null", text))
    {
        return true;
    }

    if (write_text_to_command("xclip -in -selection clipboard 2>/dev/null", text))
    {
        return true;
    }

    if (write_text_to_command("xsel --clipboard --input 2>/dev/null", text))
    {
        return true;
    }

    return false;
}

bool copy_with_wsl_windows_clipboard(const std::string& text)
{
    static constexpr const char* powershell_by_name =
        "powershell.exe "
        "-NoLogo -NoProfile -NonInteractive "
        "-Command '[Console]::InputEncoding=[System.Text.Encoding]::UTF8; "
        "Set-Clipboard -Value ([Console]::In.ReadToEnd())' "
        "2>/dev/null";

    static constexpr const char* powershell_by_path =
        "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe "
        "-NoLogo -NoProfile -NonInteractive "
        "-Command '[Console]::InputEncoding=[System.Text.Encoding]::UTF8; "
        "Set-Clipboard -Value ([Console]::In.ReadToEnd())' "
        "2>/dev/null";

    if (write_text_to_command(powershell_by_name, text))
    {
        return true;
    }

    if (write_text_to_command(powershell_by_path, text))
    {
        return true;
    }

    if (write_text_to_command("clip.exe 2>/dev/null", text))
    {
        return true;
    }

    if (write_text_to_command("/mnt/c/Windows/System32/clip.exe 2>/dev/null", text))
    {
        return true;
    }

    return false;
}

bool copy_text_to_clipboard_on_unix(const std::string& text)
{
    const bool running_in_wsl = is_wsl();

    if (is_ssh_session())
    {
        if (write_text_to_terminal_clipboard(text))
        {
            return true;
        }

        if (running_in_wsl && copy_with_wsl_windows_clipboard(text))
        {
            return true;
        }

        return copy_with_local_clipboard_tools(text);
    }

    if (running_in_wsl)
    {
        return copy_with_wsl_windows_clipboard(text) || copy_with_local_clipboard_tools(text) || write_text_to_terminal_clipboard(text);
    }

    return copy_with_local_clipboard_tools(text) || write_text_to_terminal_clipboard(text);
}

#endif

} // namespace

bool CopyTextToClipboard(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }

#ifdef _WIN32
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    const int input_length = static_cast<int>(text.size());
    const int wide_length  = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_length, nullptr, 0);
    if (wide_length <= 0)
    {
        return false;
    }

    auto* memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, static_cast<SIZE_T>(wide_length + 1) * sizeof(wchar_t));
    if (memory == nullptr)
    {
        return false;
    }

    auto* wide_text = static_cast<wchar_t*>(GlobalLock(memory));
    if (wide_text == nullptr)
    {
        GlobalFree(memory);
        return false;
    }

    const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_length, wide_text, wide_length);
    if (converted <= 0)
    {
        GlobalUnlock(memory);
        GlobalFree(memory);
        return false;
    }

    wide_text[wide_length] = L'\0';
    GlobalUnlock(memory);

    if (!OpenClipboard(nullptr))
    {
        GlobalFree(memory);
        return false;
    }

    if (!EmptyClipboard())
    {
        CloseClipboard();
        GlobalFree(memory);
        return false;
    }

    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
    {
        CloseClipboard();
        GlobalFree(memory);
        return false;
    }

    CloseClipboard();
    return true;
#else
    return copy_text_to_clipboard_on_unix(text);
#endif
}
