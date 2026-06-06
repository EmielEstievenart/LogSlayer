#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "LogView2/log_view2_data.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

namespace
{

class ScopedLogFile
{
public:
    ScopedLogFile()
    {
        const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        _path                    = std::filesystem::temp_directory_path() / ("slayerlog_log_view2_data_" + unique_suffix + ".log");
    }

    ~ScopedLogFile()
    {
        std::error_code error;
        std::filesystem::remove(_path, error);
    }

    const std::filesystem::path& path() const { return _path; }

    void write(std::string_view content) const { write_with_mode(content, std::ios::trunc); }

    void append(std::string_view content) const { write_with_mode(content, std::ios::app); }

private:
    void write_with_mode(std::string_view content, std::ios::openmode mode) const
    {
        std::ofstream output(_path, std::ios::binary | mode);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to write test log file");
        }

        output << content;
    }

    std::filesystem::path _path;
};

LogSource local_file_source(const std::filesystem::path& path)
{
    LogSource source;
    source.kind       = LogSourceKind::LocalFile;
    source.spec       = path.string();
    source.local_path = path.string();
    return source;
}

} // namespace

TEST(LogView2DataTest, CallbackReceivesFirstChangedLineFromTrackedSources)
{
    ScopedLogFile file;
    file.write("first\n");
    AllTrackedSources tracked_sources;
    std::mutex mutex;
    AllTrackedSourcesLogView2Data data(tracked_sources, mutex);

    std::optional<VisibleLineIndex> received_index;
    data.add_update_callback([&received_index](VisibleLineIndex first_changed_line) { received_index = first_changed_line; });

    const auto error = tracked_sources.open_source(local_file_source(file.path()));

    EXPECT_FALSE(error.has_value());
    ASSERT_TRUE(received_index.has_value());
    EXPECT_EQ(0, received_index->value);
}

TEST(LogView2DataTest, RemovedCallbackIsNotCalled)
{
    ScopedLogFile file;
    file.write("first\n");
    AllTrackedSources tracked_sources;
    std::mutex mutex;
    AllTrackedSourcesLogView2Data data(tracked_sources, mutex);

    int call_count         = 0;
    const auto callback_id = data.add_update_callback([&call_count](VisibleLineIndex) { ++call_count; });
    data.remove_update_callback(callback_id);

    const auto error = tracked_sources.open_source(local_file_source(file.path()));

    EXPECT_FALSE(error.has_value());
    EXPECT_EQ(0, call_count);
}

TEST(LogView2DataTest, PollCallbackReceivesAppendStartIndex)
{
    ScopedLogFile file;
    file.write("first\n");
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(tracked_sources.open_source(local_file_source(file.path())).has_value());

    std::mutex mutex;
    AllTrackedSourcesLogView2Data data(tracked_sources, mutex);

    std::optional<VisibleLineIndex> received_index;
    data.add_update_callback([&received_index](VisibleLineIndex first_changed_line) { received_index = first_changed_line; });

    file.append("second\n");
    const auto first_changed_line = tracked_sources.poll();

    ASSERT_TRUE(first_changed_line.has_value());
    EXPECT_EQ(1, first_changed_line->value);
    ASSERT_TRUE(received_index.has_value());
    EXPECT_EQ(1, received_index->value);
}

} // namespace slayerlog
