#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zstd.h>

#include "watchers/zstd_file_watcher.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_unique_test_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_zstd_file_watcher_" + unique_suffix + ".log.zst");
}

std::vector<unsigned char> compress_zstd_text(std::string_view text)
{
    const std::size_t bound = ZSTD_compressBound(text.size());
    std::vector<unsigned char> compressed(bound);
    const std::size_t written = ZSTD_compress(compressed.data(), compressed.size(), text.data(), text.size(), 1);
    if (ZSTD_isError(written) != 0)
    {
        throw std::runtime_error("Failed to compress zstd test payload");
    }

    compressed.resize(written);
    return compressed;
}

class ScopedTestFile
{
public:
    ScopedTestFile() { _path = make_unique_test_path(); }

    ~ScopedTestFile()
    {
        std::error_code error;
        std::filesystem::remove(_path, error);
    }

    const std::filesystem::path& path() const { return _path; }

    void write_zstd(std::string_view content) const { write_bytes(compress_zstd_text(content)); }

    void write_bytes(const std::vector<unsigned char>& bytes) const
    {
        std::ofstream output(_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to create test zstd file");
        }

        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

private:
    std::filesystem::path _path;
};

void expect_poll_lines(ZstdFileWatcher& watcher, const std::vector<std::string>& expected_lines)
{
    std::vector<std::string> lines {"stale data"};
    ASSERT_TRUE(watcher.poll(lines));
    EXPECT_EQ(lines, expected_lines);
}

void expect_no_poll_lines(ZstdFileWatcher& watcher)
{
    std::vector<std::string> lines {"stale data"};
    EXPECT_FALSE(watcher.poll(lines));
    EXPECT_TRUE(lines.empty());
}

} // namespace

TEST(ZstdFileWatcherTest, FirstPollReturnsDecompressedLines)
{
    ScopedTestFile test_file;
    test_file.write_zstd("first\nsecond\npartial");

    ZstdFileWatcher watcher(test_file.path().string());
    expect_poll_lines(watcher, {"first", "second"});
}

TEST(ZstdFileWatcherTest, SecondPollReturnsNothing)
{
    ScopedTestFile test_file;
    test_file.write_zstd("first\n");

    ZstdFileWatcher watcher(test_file.path().string());
    expect_poll_lines(watcher, {"first"});
    expect_no_poll_lines(watcher);
}

TEST(ZstdFileWatcherTest, MissingFileThrowsWhenPolled)
{
    const auto missing_path = make_unique_test_path();
    ZstdFileWatcher watcher(missing_path.string());

    std::vector<std::string> lines;
    EXPECT_THROW(watcher.poll(lines), std::runtime_error);
}

TEST(ZstdFileWatcherTest, InvalidZstdThrowsWhenPolled)
{
    ScopedTestFile test_file;
    test_file.write_bytes({0x01, 0x02, 0x03, 0x04});

    ZstdFileWatcher watcher(test_file.path().string());

    std::vector<std::string> lines;
    EXPECT_THROW(watcher.poll(lines), std::runtime_error);
}

} // namespace slayerlog
