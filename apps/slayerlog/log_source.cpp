#include "log_source.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace slayerlog
{

namespace
{

std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

std::string normalize_local_path_for_comparison(std::string_view file_path)
{
    const std::filesystem::path input_path(file_path);
    std::error_code error_code;
    std::filesystem::path normalized_path = std::filesystem::weakly_canonical(input_path, error_code);
    if (error_code)
    {
        error_code.clear();
        normalized_path = std::filesystem::absolute(input_path, error_code);
        if (error_code)
        {
            normalized_path = input_path.lexically_normal();
        }
        else
        {
            normalized_path = normalized_path.lexically_normal();
        }
    }

    std::string normalized_text = normalized_path.make_preferred().string();
#ifdef _WIN32
    std::transform(normalized_text.begin(), normalized_text.end(), normalized_text.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif

    return normalized_text;
}

std::string normalize_ssh_target_for_comparison(std::string_view ssh_target)
{
    const std::size_t at_sign     = ssh_target.rfind('@');
    const std::string user_prefix = at_sign == std::string_view::npos ? std::string() : std::string(ssh_target.substr(0, at_sign + 1));
    std::string host              = at_sign == std::string_view::npos ? std::string(ssh_target) : std::string(ssh_target.substr(at_sign + 1));
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return user_prefix + host;
}

std::string normalize_remote_path_for_comparison(std::string_view remote_path)
{
    return std::filesystem::path(remote_path).lexically_normal().generic_string();
}

std::string basename_for_path(const std::filesystem::path& path)
{
    const std::filesystem::path normalized_path = path.lexically_normal();
    std::filesystem::path basename              = normalized_path.filename();
    if (basename.empty())
    {
        basename = normalized_path.parent_path().filename();
    }

    return basename.string();
}

std::string source_identity(const LogSource& source)
{
    if (source.kind == LogSourceKind::SshRemoteFile)
    {
        return std::string("ssh://") + normalize_ssh_target_for_comparison(source.ssh_target) + normalize_remote_path_for_comparison(source.remote_path);
    }

    if (source.kind == LogSourceKind::LocalFolder)
    {
        return std::string("folder://") + normalize_local_path_for_comparison(source.local_folder_path);
    }

    return normalize_local_path_for_comparison(source.local_path);
}

} // namespace

LogSource parse_log_source(std::string_view text)
{
    const std::string spec = trim_text(text);
    if (spec.empty())
    {
        throw std::invalid_argument("Source path must not be empty");
    }

    constexpr std::string_view ssh_scheme = "ssh://";
    if (spec.rfind(ssh_scheme, 0) != 0)
    {
        return LogSource {
            LogSourceKind::LocalFile, spec, spec, {}, {}, {},
        };
    }

    const std::string_view remote_spec(spec);
    const std::string_view authority_and_path = remote_spec.substr(ssh_scheme.size());
    const std::size_t path_start              = authority_and_path.find('/');
    if (path_start == std::string_view::npos)
    {
        throw std::invalid_argument("Remote source must include a host and absolute path, for example ssh://user@host/var/log/app.log");
    }

    const std::string ssh_target(authority_and_path.substr(0, path_start));
    const std::string remote_path(authority_and_path.substr(path_start));
    if (ssh_target.empty() || remote_path.empty() || remote_path == "/")
    {
        throw std::invalid_argument("Remote source must include a host and absolute path, for example ssh://user@host/var/log/app.log");
    }

    return LogSource {
        LogSourceKind::SshRemoteFile, spec, {}, {}, ssh_target, remote_path,
    };
}

LogSource make_local_folder_source(std::string_view text)
{
    const std::string spec = trim_text(text);
    if (spec.empty())
    {
        throw std::invalid_argument("Folder path must not be empty");
    }

    return LogSource {
        LogSourceKind::LocalFolder, spec, {}, spec, {}, {},
    };
}

std::string source_display_path(const LogSource& source)
{
    if (source.kind == LogSourceKind::LocalFolder)
    {
        return source.local_folder_path;
    }

    if (source.kind == LogSourceKind::SshRemoteFile)
    {
        return source.spec;
    }

    return source.local_path;
}

std::string source_basename(const LogSource& source)
{
    if (source.kind == LogSourceKind::LocalFolder)
    {
        return basename_for_path(std::filesystem::path(source.local_folder_path));
    }

    if (source.kind == LogSourceKind::SshRemoteFile)
    {
        return std::filesystem::path(source.remote_path).filename().string();
    }

    return std::filesystem::path(source.local_path).filename().string();
}

bool same_source(const LogSource& lhs, const LogSource& rhs)
{
    return source_identity(lhs) == source_identity(rhs);
}

std::vector<std::string> build_source_labels(const std::vector<LogSource>& sources)
{
    std::unordered_map<std::string, int> basename_counts;
    for (const auto& source : sources)
    {
        ++basename_counts[source_basename(source)];
    }

    std::vector<std::string> labels;
    labels.reserve(sources.size());
    for (const auto& source : sources)
    {
        const std::string basename = source_basename(source);
        if (!basename.empty() && basename_counts[basename] == 1)
        {
            labels.push_back(basename);
        }
        else
        {
            labels.push_back(source_display_path(source));
        }
    }

    return labels;
}

} // namespace slayerlog
