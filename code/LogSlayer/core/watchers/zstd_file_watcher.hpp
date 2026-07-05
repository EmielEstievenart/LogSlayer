#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <zstd.h>

#include "log_watcher_base.hpp"
#include "stream_line_buffer.hpp"

namespace slayerlog
{

/**
 * One-shot reader for a zstd-compressed log file. The compressed bytes are read once,
 * then decompressed incrementally: each poll emits at most max_output_bytes_per_poll of
 * decompressed data so callers polling under a lock stay responsive. backlog_pending()
 * reports whether decompression still has input left.
 */
class ZstdFileWatcher : public LogWatcherBase
{
public:
    static constexpr std::size_t default_max_output_bytes_per_poll = 4 * 1024 * 1024;

    explicit ZstdFileWatcher(std::string file_path, std::size_t max_output_bytes_per_poll = default_max_output_bytes_per_poll);

    ZstdFileWatcher(const ZstdFileWatcher&)            = delete;
    ZstdFileWatcher& operator=(const ZstdFileWatcher&) = delete;

protected:
    bool poll_locked(std::vector<std::string>& lines) override;
    bool backlog_pending_locked() const override;

private:
    struct DStreamDeleter
    {
        void operator()(ZSTD_DStream* stream) const { ZSTD_freeDStream(stream); }
    };

    static std::string read_binary_file(const std::string& path);

    void initialize_stream();
    void finish_decompression();

    std::string _file_path;
    std::size_t _max_output_bytes_per_poll = default_max_output_bytes_per_poll;

    std::string _compressed_bytes;
    std::size_t _compressed_read_pos = 0;
    std::unique_ptr<ZSTD_DStream, DStreamDeleter> _stream;
    StreamLineBuffer _line_buffer;
    bool _started        = false;
    bool _frame_complete = false;
    bool _consumed       = false;
};

} // namespace slayerlog
