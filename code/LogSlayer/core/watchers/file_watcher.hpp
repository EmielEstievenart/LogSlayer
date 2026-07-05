#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "log_watcher_base.hpp"

namespace slayerlog
{

class FileWatcher : public LogWatcherBase
{
public:
    /// Bounds the bytes ingested per poll so callers never block on an unbounded read;
    /// the remainder is reported through backlog_pending() and picked up by later polls.
    static constexpr std::size_t default_max_read_bytes_per_poll = 4 * 1024 * 1024;

    explicit FileWatcher(std::string file_path, std::size_t max_read_bytes_per_poll = default_max_read_bytes_per_poll);

    FileWatcher(const FileWatcher&)            = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

protected:
    bool poll_locked(std::vector<std::string>& lines) override;
    bool backlog_pending_locked() const override;

private:
    struct State
    {
        std::uintmax_t offset = 0;
        std::string pending_fragment;
        std::string offset_tail_bytes;
        bool awaiting_regrowth_after_shrink  = false;
        std::uintmax_t shrink_candidate_size = 0;
    };

    static void parse_lines_from_chunk(std::string chunk, State& state, std::vector<std::string>& lines);
    static void update_offset_tail_bytes(std::string_view chunk, State& state);
    static std::string read_file_tail(const std::string& path, std::uintmax_t offset, std::size_t max_bytes);
    static std::string read_window_ending_at(const std::string& path, std::uintmax_t offset);
    static std::uintmax_t get_file_size(const std::string& path);

    bool collect_update_locked(std::vector<std::string>& lines);

    std::string _file_path;
    std::size_t _max_read_bytes_per_poll = default_max_read_bytes_per_poll;
    State _state;
    bool _backlog_pending = false;
};

} // namespace slayerlog
