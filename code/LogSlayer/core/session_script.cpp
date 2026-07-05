#include "session_script.hpp"

#include <algorithm>
#include <cctype>

namespace slayerlog
{

namespace
{

/// Quotes one argument for a Windows command line following the CRT parsing
/// rules (backslash runs before a quote are doubled), then doubles every %
/// so cmd.exe's variable expansion leaves the text intact inside a batch file.
std::string quote_batch_argument(const std::string& text)
{
    std::string quoted;
    quoted.reserve(text.size() + 2);
    quoted.push_back('"');

    std::size_t pending_backslashes = 0;
    for (const char character : text)
    {
        if (character == '\\')
        {
            ++pending_backslashes;
            continue;
        }

        if (character == '"')
        {
            quoted.append(pending_backslashes * 2 + 1, '\\');
            quoted.push_back('"');
            pending_backslashes = 0;
            continue;
        }

        quoted.append(pending_backslashes, '\\');
        pending_backslashes = 0;
        quoted.push_back(character);
    }

    quoted.append(pending_backslashes * 2, '\\');
    quoted.push_back('"');

    std::string percent_escaped;
    percent_escaped.reserve(quoted.size());
    for (const char character : quoted)
    {
        if (character == '%')
        {
            percent_escaped.push_back('%');
        }
        percent_escaped.push_back(character);
    }

    return percent_escaped;
}

std::string quote_shell_argument(const std::string& text)
{
    std::string quoted = "'";
    for (const char character : text)
    {
        if (character == '\'')
        {
            quoted += "'\\''";
            continue;
        }
        quoted.push_back(character);
    }
    quoted.push_back('\'');
    return quoted;
}

std::string quote_argument(SessionScriptKind kind, const std::string& text)
{
    return kind == SessionScriptKind::WindowsBatch ? quote_batch_argument(text) : quote_shell_argument(text);
}

} // namespace

std::optional<SessionScriptKind> session_script_kind_for_path(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    if (extension == ".bat" || extension == ".cmd")
    {
        return SessionScriptKind::WindowsBatch;
    }
    if (extension == ".sh")
    {
        return SessionScriptKind::PosixShell;
    }

    return std::nullopt;
}

SessionScriptKind native_session_script_kind()
{
#if defined(_WIN32)
    return SessionScriptKind::WindowsBatch;
#else
    return SessionScriptKind::PosixShell;
#endif
}

std::string build_session_script(SessionScriptKind kind, const std::string& executable, const std::vector<std::string>& command_lines)
{
    const bool batch               = kind == SessionScriptKind::WindowsBatch;
    const std::string line_ending  = batch ? "\r\n" : "\n";
    const std::string continuation = batch ? " ^" : " \\";

    std::string script;
    script += batch ? "@echo off" : "#!/bin/sh";
    script += line_ending;

    script += quote_argument(kind, executable);
    for (std::size_t command_index = 0; command_index < command_lines.size(); ++command_index)
    {
        script += continuation;
        script += line_ending;
        script += "    --cmd ";
        script += quote_argument(kind, command_lines[command_index]);
    }
    script += line_ending;

    return script;
}

std::string build_session_invocation_line(SessionScriptKind kind, const std::string& executable, const std::vector<std::string>& command_lines)
{
    std::string invocation = quote_argument(kind, executable);
    for (const auto& command_line : command_lines)
    {
        invocation += " --cmd ";
        invocation += quote_argument(kind, command_line);
    }

    return invocation;
}

} // namespace slayerlog
