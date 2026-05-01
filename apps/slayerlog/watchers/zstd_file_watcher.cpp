#include "zstd_file_watcher.hpp"

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <zstd.h>

#include "debug_log.hpp"

namespace slayerlog
{

namespace
{

struct DStreamDeleter
{
    void operator()(ZSTD_DStream* stream) const { ZSTD_freeDStream(stream); }
};

} // namespace

ZstdFileWatcher::ZstdFileWatcher(std::string file_path) : _file_path(std::move(file_path))
{
    SLAYERLOG_LOG_INFO("Created zstd file watcher for file=" << _file_path);
}

bool ZstdFileWatcher::poll_locked(std::vector<std::string>& lines)
{
    if (_consumed)
    {
        return false;
    }

    lines     = split_lines(decompress_zstd_bytes(read_binary_file(_file_path), _file_path));
    _consumed = true;
    return !lines.empty();
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

std::string ZstdFileWatcher::decompress_zstd_bytes(const std::string& compressed_bytes, const std::string& path)
{
    std::unique_ptr<ZSTD_DStream, DStreamDeleter> stream(ZSTD_createDStream());
    if (stream == nullptr)
    {
        throw std::runtime_error("Failed to create zstd stream for file: " + path);
    }

    std::size_t result = ZSTD_initDStream(stream.get());
    if (ZSTD_isError(result) != 0)
    {
        throw std::runtime_error("Failed to initialize zstd stream for file: " + path);
    }

    ZSTD_inBuffer input {compressed_bytes.data(), compressed_bytes.size(), 0};
    std::array<char, 1 << 15> buffer {};
    std::string output;

    while (input.pos < input.size)
    {
        ZSTD_outBuffer out {buffer.data(), buffer.size(), 0};
        result = ZSTD_decompressStream(stream.get(), &out, &input);
        if (ZSTD_isError(result) != 0)
        {
            throw std::runtime_error("Failed to decompress zstd file: " + path);
        }

        output.append(buffer.data(), out.pos);

        if (result == 0 && input.pos < input.size)
        {
            result = ZSTD_initDStream(stream.get());
            if (ZSTD_isError(result) != 0)
            {
                throw std::runtime_error("Failed to continue zstd decompression for file: " + path);
            }
        }
    }

    if (result != 0)
    {
        throw std::runtime_error("Incomplete zstd data in file: " + path);
    }

    return output;
}

std::vector<std::string> ZstdFileWatcher::split_lines(std::string text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size())
    {
        const auto newline = text.find('\n', start);
        if (newline == std::string::npos)
        {
            break;
        }

        std::string line = text.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        lines.push_back(std::move(line));
        start = newline + 1;
    }

    return lines;
}

} // namespace slayerlog
