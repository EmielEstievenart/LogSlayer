#pragma once

#include <string>
#include <vector>

#include "log_watcher_base.hpp"

namespace slayerlog
{

class ZstdFileWatcher : public LogWatcherBase
{
public:
    explicit ZstdFileWatcher(std::string file_path);

    ZstdFileWatcher(const ZstdFileWatcher&)            = delete;
    ZstdFileWatcher& operator=(const ZstdFileWatcher&) = delete;

protected:
    bool poll_locked(std::vector<std::string>& lines) override;

private:
    static std::string read_binary_file(const std::string& path);
    static std::string decompress_zstd_bytes(const std::string& compressed_bytes, const std::string& path);
    static std::vector<std::string> split_lines(std::string text);

    std::string _file_path;
    bool _consumed = false;
};

} // namespace slayerlog
