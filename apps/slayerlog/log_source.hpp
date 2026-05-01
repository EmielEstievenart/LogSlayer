#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace slayerlog
{

enum class LogSourceKind
{
    LocalFile,
    LocalFolder,
    SshRemoteFile,
};

struct LogSource
{
    LogSourceKind kind = LogSourceKind::LocalFile;
    std::string spec;
    std::string local_path;
    std::string local_folder_path;
    std::string ssh_target;
    std::string remote_path;
};

LogSource parse_log_source(std::string_view text);
LogSource make_local_folder_source(std::string_view text);
std::string source_display_path(const LogSource& source);
std::string source_basename(const LogSource& source);
bool same_source(const LogSource& lhs, const LogSource& rhs);
std::vector<std::string> build_source_labels(const std::vector<LogSource>& sources);

} // namespace slayerlog
