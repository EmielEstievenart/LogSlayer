#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace slayerlog
{

/**
 * @brief Type of log source.
 */
enum class LogSourceKind
{
    LocalFile,     ///< A single local log file.
    LocalFolder,   ///< A local folder containing log files.
    SshRemoteFile, ///< A remote log file accessed over SSH.
};

/**
 * @brief Describes one configured log source.
 *
 * The active fields depend on @ref kind:
 * local files use @ref local_path, local folders use @ref local_folder_path,
 * and SSH sources use @ref ssh_target plus @ref remote_path.
 */
struct LogSource
{
    LogSourceKind kind = LogSourceKind::LocalFile; ///< Source type.
    std::string spec;                              ///< Original user-facing source specification.
    std::string local_path;                        ///< Local file path for @ref LogSourceKind::LocalFile.
    std::string local_folder_path;                 ///< Folder path for @ref LogSourceKind::LocalFolder.
    std::string ssh_target;                        ///< SSH target, for example `user@host`.
    std::string remote_path;                       ///< Absolute remote path for SSH sources.
};

/**
 * @brief Parses a local file or `ssh://` remote file source.
 *
 * @throws std::invalid_argument If the input is empty or the SSH source is malformed.
 */
LogSource parse_log_source(std::string_view text);

/**
 * @brief Creates a local folder source from a path.
 *
 * @throws std::invalid_argument If the input is empty.
 */
LogSource make_local_folder_source(std::string_view text);

/**
 * @brief Returns the full user-facing path or source specification.
 *
 * This is the value to show when the source must be unambiguous.
 */
std::string source_display_path(const LogSource& source);

/**
 * @brief Returns the short name of the source path.
 *
 * This is usually the filename for file sources, or the folder name for folder sources.
 */
std::string source_basename(const LogSource& source);

/**
 * @brief Returns whether two sources refer to the same normalized source.
 */
bool same_source(const LogSource& lhs, const LogSource& rhs);

/**
 * @brief Returns a stable, normalized identity string for a source.
 *
 * Two sources that refer to the same underlying log compare equal via this
 * string. It is the basis for both @ref same_source and mnemonic selection.
 */
std::string source_identity(const LogSource& source);

/**
 * @brief Builds readable source labels.
 *
 * Uses the basename when it is unique; otherwise falls back to the full display path.
 * The returned labels keep the same order as @p sources.
 */
std::vector<std::string> build_source_labels(const std::vector<LogSource>& sources);

} // namespace slayerlog