#include "zstd_file_watcher.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "debug_log.hpp"

namespace slayerlog
{

ZstdFileWatcher::ZstdFileWatcher(std::string file_path, std::size_t max_output_bytes_per_poll) : _file_path(std::move(file_path)), _max_output_bytes_per_poll(std::max<std::size_t>(1, max_output_bytes_per_poll))
{
    SLAYERLOG_LOG_INFO("Created zstd file watcher for file=" << _file_path << " max_output_bytes_per_poll=" << _max_output_bytes_per_poll);
}

bool ZstdFileWatcher::poll_locked(std::vector<std::string>& lines)
{
    if (_consumed)
    {
        return false;
    }

    if (!_started)
    {
        initialize_stream();
    }

    std::array<char, 1 << 15> buffer {};
    std::size_t produced_bytes = 0;

    while (_compressed_read_pos < _compressed_bytes.size() && produced_bytes < _max_output_bytes_per_poll)
    {
        ZSTD_inBuffer input {_compressed_bytes.data(), _compressed_bytes.size(), _compressed_read_pos};
        ZSTD_outBuffer output {buffer.data(), buffer.size(), 0};

        const std::size_t result = ZSTD_decompressStream(_stream.get(), &output, &input);
        if (ZSTD_isError(result) != 0)
        {
            throw std::runtime_error("Failed to decompress zstd file: " + _file_path);
        }

        _compressed_read_pos = input.pos;
        _frame_complete      = result == 0;

        if (output.pos > 0)
        {
            produced_bytes += output.pos;
            _line_buffer.append(std::string_view(buffer.data(), output.pos), lines);
        }

        // A frame ended with input still left: the file holds multiple concatenated
        // frames, so reset the stream for the next one.
        if (result == 0 && _compressed_read_pos < _compressed_bytes.size())
        {
            const std::size_t init_result = ZSTD_initDStream(_stream.get());
            if (ZSTD_isError(init_result) != 0)
            {
                throw std::runtime_error("Failed to continue zstd decompression for file: " + _file_path);
            }
        }
    }

    if (_compressed_read_pos >= _compressed_bytes.size())
    {
        finish_decompression();
    }

    return !lines.empty();
}

bool ZstdFileWatcher::backlog_pending_locked() const
{
    return _started && !_consumed;
}

std::string ZstdFileWatcher::read_binary_file(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        throw std::runtime_error("Failed to open file: " + path);
    }

    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void ZstdFileWatcher::initialize_stream()
{
    _compressed_bytes = read_binary_file(_file_path);

    _stream.reset(ZSTD_createDStream());
    if (_stream == nullptr)
    {
        throw std::runtime_error("Failed to create zstd stream for file: " + _file_path);
    }

    const std::size_t result = ZSTD_initDStream(_stream.get());
    if (ZSTD_isError(result) != 0)
    {
        throw std::runtime_error("Failed to initialize zstd stream for file: " + _file_path);
    }

    _started = true;
}

void ZstdFileWatcher::finish_decompression()
{
    // Matches the historical one-shot behavior: data that does not end on a frame
    // boundary (including an empty file) is an error, and an unterminated final line
    // is dropped rather than emitted.
    const bool incomplete = !_frame_complete;

    _consumed = true;
    _stream.reset();
    _compressed_bytes.clear();
    _compressed_bytes.shrink_to_fit();
    _compressed_read_pos = 0;
    _line_buffer.discard_pending_fragment();

    if (incomplete)
    {
        throw std::runtime_error("Incomplete zstd data in file: " + _file_path);
    }
}

} // namespace slayerlog
