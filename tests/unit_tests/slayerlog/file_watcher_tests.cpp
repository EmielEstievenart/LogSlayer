#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "watchers/file_watcher.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_unique_test_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_file_watcher_" + unique_suffix + ".log");
}

class ScopedTestFile
{
public:
    ScopedTestFile()
    {
        _path = make_unique_test_path();

        std::ofstream output(_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to create test file");
        }
    }

    ~ScopedTestFile()
    {
        std::error_code error;
        std::filesystem::remove(_path, error);
    }

    const std::filesystem::path& path() const { return _path; }

    void write(const std::string& content) const
    {
        std::ofstream output(_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << content;
        output.close();
        ASSERT_TRUE(output.good());
    }

    void append(const std::string& content) const
    {
        std::ofstream output(_path, std::ios::binary | std::ios::app);
        ASSERT_TRUE(output.is_open());
        output << content;
        output.close();
        ASSERT_TRUE(output.good());
    }

private:
    std::filesystem::path _path;
};

void expect_poll_lines(FileWatcher& watcher, const std::vector<std::string>& expected_lines)
{
    std::vector<std::string> lines {"stale data"};
    ASSERT_TRUE(watcher.poll(lines));
    EXPECT_EQ(lines, expected_lines);
}

void expect_no_poll_lines(FileWatcher& watcher)
{
    std::vector<std::string> lines {"stale data"};
    EXPECT_FALSE(watcher.poll(lines));
    EXPECT_TRUE(lines.empty());
}

} // namespace

TEST(FileWatcherTest, FirstPollReturnsFullExistingFileContents)
{
    ScopedTestFile test_file;
    test_file.write("first\nsecond\n");

    FileWatcher watcher(test_file.path().string());
    expect_poll_lines(watcher, {"first", "second"});
}

TEST(FileWatcherTest, ReadsOnlyNewlyAppendedLinesWhenFileGrows)
{
    ScopedTestFile test_file;

    FileWatcher watcher(test_file.path().string());
    expect_no_poll_lines(watcher);

    test_file.append("first\n");
    expect_poll_lines(watcher, {"first"});

    test_file.append("second\nthird\n");
    expect_poll_lines(watcher, {"second", "third"});

    expect_no_poll_lines(watcher);
}

TEST(FileWatcherTest, ShrinkingFileReturnsCurrentRolloverContents)
{
    ScopedTestFile test_file;
    test_file.write("before rollover content\n");

    FileWatcher watcher(test_file.path().string());
    expect_poll_lines(watcher, {"before rollover content"});

    test_file.write("after\n");
    // The first shrink poll only arms rollover detection so truncate-and-rewrite saves
    // can regrow without rewinding immediately.
    expect_no_poll_lines(watcher);
    expect_poll_lines(watcher, {"after"});
}

TEST(FileWatcherTest, RolloverAfterWatchingReadsNewFileFromStart)
{
    ScopedTestFile test_file;

    FileWatcher watcher(test_file.path().string());
    test_file.append("before rollover\nsecond line\n");
    expect_poll_lines(watcher, {"before rollover", "second line"});

    test_file.write("");
    // The watcher waits for the shrink to stabilize before declaring rollover.
    expect_no_poll_lines(watcher);

    test_file.append("after rollover\n");
    // A different smaller size means the file is still being rewritten, so one more
    // probe is required before reading the replacement contents from the start.
    expect_no_poll_lines(watcher);

    expect_poll_lines(watcher, {"after rollover"});
}

TEST(FileWatcherTest, RolloverDoesNotEmitIncompleteLastLineUntilItIsCompleted)
{
    ScopedTestFile test_file;
    test_file.write("before rollover content\n");

    FileWatcher watcher(test_file.path().string());
    expect_poll_lines(watcher, {"before rollover content"});

    test_file.write("");
    // The first shrink poll only arms rollover detection.
    expect_no_poll_lines(watcher);

    test_file.write("after rollover\npartial");
    // The file is still changing size after the shrink, so wait for it to settle.
    expect_no_poll_lines(watcher);

    expect_poll_lines(watcher, {"after rollover"});

    test_file.append(" line\n");
    expect_poll_lines(watcher, {"partial line"});
}

TEST(FileWatcherTest, ShrinkingToEmptyResetsOffsetAndLaterReadsFromStart)
{
    ScopedTestFile test_file;
    test_file.write("before\n");

    FileWatcher watcher(test_file.path().string());
    expect_poll_lines(watcher, {"before"});

    test_file.write("");
    expect_no_poll_lines(watcher);

    test_file.write("fresh line\n");
    expect_poll_lines(watcher, {"fresh line"});
}

TEST(FileWatcherTest, HoldsIncompleteLineUntilLaterAppendCompletesIt)
{
    ScopedTestFile test_file;

    FileWatcher watcher(test_file.path().string());
    expect_no_poll_lines(watcher);

    test_file.append("partial");
    expect_no_poll_lines(watcher);

    test_file.append(" line\nnext\n");
    expect_poll_lines(watcher, {"partial line", "next"});

    expect_no_poll_lines(watcher);
}

TEST(FileWatcherTest, TruncatedRewriteThatRegrowsPastOldOffsetSkipsSeenLinesAndWaitsForCompletion)
{
    ScopedTestFile test_file;
    test_file.write("first\nsecond\nthird");

    FileWatcher watcher(test_file.path().string());
    expect_poll_lines(watcher, {"first", "second"});

    test_file.write("first\n");
    // This shrink may be a transient truncate-and-rewrite save, so the watcher waits
    // for regrowth before deciding whether it should rewind to the beginning.
    expect_no_poll_lines(watcher);

    test_file.write("first\nsecond\nthird\nfourth");
    expect_poll_lines(watcher, {"third"});

    test_file.append("\n");
    expect_poll_lines(watcher, {"fourth"});
}

TEST(FileWatcherTest, MissingFileThrowsWhenPolled)
{
    const auto missing_path = make_unique_test_path();
    FileWatcher watcher(missing_path.string());

    std::vector<std::string> lines;
    EXPECT_THROW(watcher.poll(lines), std::filesystem::filesystem_error);
}

} // namespace slayerlog
