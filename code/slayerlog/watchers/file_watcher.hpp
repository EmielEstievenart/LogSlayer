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
    explicit FileWatcher(std::string file_path);

    FileWatcher(const FileWatcher&)            = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

protected:
    bool poll_locked(std::vector<std::string>& lines) override;

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
    static std::string read_file_tail(const std::string& path, std::uintmax_t offset);
    static std::string read_window_ending_at(const std::string& path, std::uintmax_t offset);
    static std::uintmax_t get_file_size(const std::string& path);

    bool collect_update_locked(std::vector<std::string>& lines);

    std::string _file_path;
    State _state;
};

} // namespace slayerlog
