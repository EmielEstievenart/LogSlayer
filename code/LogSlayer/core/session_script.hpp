#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace slayerlog
{

enum class SessionScriptKind
{
    WindowsBatch,
    PosixShell,
};

/// The script kind implied by @p path's extension: .bat/.cmd → WindowsBatch,
/// .sh → PosixShell, anything else → std::nullopt.
std::optional<SessionScriptKind> session_script_kind_for_path(const std::filesystem::path& path);

/// The script kind matching the platform this binary was built for.
SessionScriptKind native_session_script_kind();

/**
 * @brief Renders a runnable script that starts @p executable with one --cmd
 * argument per snapshot command line.
 *
 * Batch scripts use CRLF line endings, caret continuations, CRT-style argument
 * quoting, and %% escaping; shell scripts use a #!/bin/sh header, backslash
 * continuations, and single-quote escaping.
 */
std::string build_session_script(SessionScriptKind kind, const std::string& executable, const std::vector<std::string>& command_lines);

/// The same invocation as build_session_script on a single line, for pasting
/// into a terminal or a hand-written script.
std::string build_session_invocation_line(SessionScriptKind kind, const std::string& executable, const std::vector<std::string>& command_lines);

} // namespace slayerlog
