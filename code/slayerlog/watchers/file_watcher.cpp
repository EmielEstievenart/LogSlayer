#include "debug_log.hpp"
#include "file_watcher.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace slayerlog
{

namespace
{

constexpr std::size_t tail_verification_window_size = 256;

std::string quote_for_log(std::string_view text)
{
    constexpr std::size_t max_logged_characters = 240;

    std::ostringstream output;
    output << '"';

    const auto characters_to_log = std::min(text.size(), max_logged_characters);
    for (std::size_t index = 0; index < characters_to_log; ++index)
    {
        const unsigned char character = static_cast<unsigned char>(text[index]);
        switch (character)
        {
        case '\\':
            output << "\\\\";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        case '"':
            output << "\\\"";
            break;
        default:
            if (std::isprint(character) != 0)
            {
                output << static_cast<char>(character);
            }
            else
            {
                output << "\\x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(character) << std::dec << std::nouppercase << std::setfill(' ');
            }
            break;
        }
    }

    if (text.size() > max_logged_characters)
    {
        output << "...(" << text.size() << " bytes total)";
    }

    output << '"';
    return output.str();
}

std::string describe_lines_for_log(const std::vector<std::string>& lines)
{
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        if (index > 0)
        {
            output << ", ";
        }

        output << quote_for_log(lines[index]);
    }
    output << "]";
    return output.str();
}

} // namespace

void FileWatcher::parse_lines_from_chunk(std::string chunk, FileWatcher::State& state, std::vector<std::string>& lines)
{
    SLAYERLOG_LOG_TRACE("parse_lines_from_chunk begin chunk_bytes=" << chunk.size() << " chunk=" << quote_for_log(chunk) << " pending_fragment_bytes=" << state.pending_fragment.size()
                                                                    << " pending_fragment=" << quote_for_log(state.pending_fragment));

    if (!state.pending_fragment.empty())
    {
        SLAYERLOG_LOG_TRACE("Prepending pending fragment " << quote_for_log(state.pending_fragment) << " to new chunk");
        chunk.insert(0, state.pending_fragment);
        state.pending_fragment.clear();
    }

    std::size_t start = 0;
    while (start < chunk.size())
    {
        const auto newline = chunk.find('\n', start);
        if (newline == std::string::npos)
        {
            state.pending_fragment = chunk.substr(start);
            SLAYERLOG_LOG_TRACE("Stored trailing pending fragment bytes=" << state.pending_fragment.size() << " fragment=" << quote_for_log(state.pending_fragment));
            break;
        }

        auto line = chunk.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        SLAYERLOG_LOG_TRACE("Parsed complete line " << quote_for_log(line));
        lines.push_back(std::move(line));
        start = newline + 1;
    }

    SLAYERLOG_LOG_TRACE("parse_lines_from_chunk end produced_lines=" << lines.size() << " lines=" << describe_lines_for_log(lines) << " remaining_pending_bytes=" << state.pending_fragment.size()
                                                                     << " remaining_pending=" << quote_for_log(state.pending_fragment));
}

void FileWatcher::update_offset_tail_bytes(std::string_view chunk, FileWatcher::State& state)
{
    if (chunk.size() >= tail_verification_window_size)
    {
        state.offset_tail_bytes.assign(chunk.substr(chunk.size() - tail_verification_window_size));
        return;
    }

    if (state.offset_tail_bytes.size() + chunk.size() > tail_verification_window_size)
    {
        const auto erase_count = state.offset_tail_bytes.size() + chunk.size() - tail_verification_window_size;
        state.offset_tail_bytes.erase(0, erase_count);
    }

    state.offset_tail_bytes.append(chunk.data(), chunk.size());
}

std::string FileWatcher::read_file_tail(const std::string& path, std::uintmax_t offset)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open file: " + path);
    }

    input.seekg(0, std::ios::end);
    const auto size_position = input.tellg();
    if (size_position < 0)
    {
        throw std::runtime_error("Failed to determine file size: " + path);
    }

    const auto file_size = static_cast<std::uintmax_t>(size_position);
    if (offset >= file_size)
    {
        return {};
    }

    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    std::string chunk(file_size - offset, '\0');
    input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    chunk.resize(static_cast<std::size_t>(input.gcount()));
    return chunk;
}

std::string FileWatcher::read_window_ending_at(const std::string& path, std::uintmax_t offset)
{
    if (offset == 0)
    {
        return {};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open file: " + path);
    }

    const auto window_start = (offset > tail_verification_window_size) ? (offset - tail_verification_window_size) : 0;
    const auto window_size  = offset - window_start;

    input.seekg(static_cast<std::streamoff>(window_start), std::ios::beg);
    std::string window(static_cast<std::size_t>(window_size), '\0');
    input.read(window.data(), static_cast<std::streamsize>(window.size()));
    window.resize(static_cast<std::size_t>(input.gcount()));
    return window;
}

std::uintmax_t FileWatcher::get_file_size(const std::string& path)
{
    return std::filesystem::file_size(std::filesystem::path(path));
}

FileWatcher::FileWatcher(std::string file_path) : _file_path(std::move(file_path))
{
    SLAYERLOG_LOG_INFO("Created file watcher for file=" << _file_path);
}

bool FileWatcher::poll_locked(std::vector<std::string>& lines)
{
    SLAYERLOG_LOG_TRACE("poll begin file=" << _file_path);
    SLAYERLOG_LOG_TRACE("poll locked file=" << _file_path << " state.offset=" << _state.offset << " state.pending_fragment_bytes=" << _state.pending_fragment.size()
                                            << " state.awaiting_regrowth_after_shrink=" << _state.awaiting_regrowth_after_shrink << " state.shrink_candidate_size=" << _state.shrink_candidate_size);
    if (!collect_update_locked(lines))
    {
        SLAYERLOG_LOG_TRACE("poll end file=" << _file_path << " returned=false");
        return false;
    }

    SLAYERLOG_LOG_DEBUG("poll end file=" << _file_path << " returned=true line_count=" << lines.size() << " lines=" << describe_lines_for_log(lines));
    return true;
}

bool FileWatcher::collect_update_locked(std::vector<std::string>& lines)
{
    auto file_size = get_file_size(_file_path);
    SLAYERLOG_LOG_TRACE("collect_update_locked file=" << _file_path << " file_size=" << file_size << " offset=" << _state.offset << " pending_fragment_bytes=" << _state.pending_fragment.size()
                                                      << " awaiting_regrowth=" << _state.awaiting_regrowth_after_shrink << " shrink_candidate_size=" << _state.shrink_candidate_size);

    if (file_size < _state.offset)
    {
        SLAYERLOG_LOG_DEBUG("Detected shrink file=" << _file_path << " file_size=" << file_size << " previous_offset=" << _state.offset);
        // Some editors save by truncating and rewriting the whole file. Wait briefly to
        // see whether the file grows back to the old offset before treating it as rollover.
        if (!_state.awaiting_regrowth_after_shrink)
        {
            _state.awaiting_regrowth_after_shrink = true;
            _state.shrink_candidate_size          = file_size;
            SLAYERLOG_LOG_DEBUG("Armed regrowth detection file=" << _file_path << " shrink_candidate_size=" << _state.shrink_candidate_size);
            return false;
        }

        if (file_size != _state.shrink_candidate_size)
        {
            SLAYERLOG_LOG_DEBUG("Shrink candidate changed while waiting file=" << _file_path << " previous_candidate=" << _state.shrink_candidate_size << " new_candidate=" << file_size);
            _state.shrink_candidate_size = file_size;
            return false;
        }

        SLAYERLOG_LOG_DEBUG("Confirmed rollover after stable shrink file=" << _file_path << "; resetting state");
        _state    = State {};
        file_size = get_file_size(_file_path);
        SLAYERLOG_LOG_TRACE("After rollover reset file=" << _file_path << " reloaded_file_size=" << file_size);
    }
    else if (_state.awaiting_regrowth_after_shrink)
    {
        const auto window = read_window_ending_at(_file_path, _state.offset);
        SLAYERLOG_LOG_TRACE("Comparing regrowth window file=" << _file_path << " window_bytes=" << window.size() << " expected_tail_bytes=" << _state.offset_tail_bytes.size() << " window=" << quote_for_log(window)
                                                              << " expected_tail=" << quote_for_log(_state.offset_tail_bytes));
        if (window != _state.offset_tail_bytes)
        {
            SLAYERLOG_LOG_DEBUG("Regrowth no longer matches previous tail file=" << _file_path << "; treating as rollover");
            _state    = State {};
            file_size = get_file_size(_file_path);
            SLAYERLOG_LOG_TRACE("After regrowth mismatch reset file=" << _file_path << " reloaded_file_size=" << file_size);
        }
        else
        {
            _state.awaiting_regrowth_after_shrink = false;
            SLAYERLOG_LOG_DEBUG("Regrowth matched previous tail file=" << _file_path << "; continuing without rewind");
        }
    }

    if (file_size == _state.offset)
    {
        SLAYERLOG_LOG_TRACE("No new bytes available file=" << _file_path << " offset=" << _state.offset);
        return false;
    }

    auto chunk = read_file_tail(_file_path, _state.offset);
    SLAYERLOG_LOG_TRACE("Read file tail file=" << _file_path << " previous_offset=" << _state.offset << " new_file_size=" << file_size << " chunk_bytes=" << chunk.size() << " chunk=" << quote_for_log(chunk));
    _state.offset = file_size;
    update_offset_tail_bytes(chunk, _state);
    if (chunk.empty())
    {
        SLAYERLOG_LOG_TRACE("Tail read produced empty chunk file=" << _file_path);
        return false;
    }

    parse_lines_from_chunk(std::move(chunk), _state, lines);
    if (lines.empty())
    {
        SLAYERLOG_LOG_DEBUG("Chunk produced no complete lines file=" << _file_path << " pending_fragment_bytes=" << _state.pending_fragment.size() << " pending_fragment=" << quote_for_log(_state.pending_fragment));
        return false;
    }

    SLAYERLOG_LOG_DEBUG("collect_update_locked returning lines file=" << _file_path << " line_count=" << lines.size() << " lines=" << describe_lines_for_log(lines) << " pending_fragment_bytes=" << _state.pending_fragment.size()
                                                                      << " pending_fragment=" << quote_for_log(_state.pending_fragment));
    return true;
}

} // namespace slayerlog
