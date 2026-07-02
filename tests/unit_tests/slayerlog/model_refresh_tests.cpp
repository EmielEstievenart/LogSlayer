#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "model_refresh.hpp"
#include "redraw_scheduler.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/tracked_source_factory.hpp"

namespace slayerlog
{

namespace
{

class RecordingRedrawScheduler final : public RedrawScheduler
{
public:
    void request_redraw() override { ++redraw_count; }

    int redraw_count = 0;
};

std::filesystem::path make_temp_log_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_model_refresh_" + unique_suffix + ".log");
}

void write_lines(const std::filesystem::path& log_path, const std::string& content, std::ios::openmode mode)
{
    std::ofstream output(log_path, std::ios::binary | mode);
    ASSERT_TRUE(output.is_open());
    output << content;
}

} // namespace

TEST(ModelRefreshTest, ReloadRebuildsProcessedSourcesAndRequestsRedraw)
{
    const auto log_path = make_temp_log_path();
    write_lines(log_path, "first line\nsecond line\n", std::ios::trunc);

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());

    AllProcessedSources processed_sources;
    RecordingRedrawScheduler redraw_scheduler;
    reload_processed_sources(tracked_sources, processed_sources, redraw_scheduler);

    EXPECT_EQ(processed_sources.total_line_count(), 2U);
    EXPECT_EQ(redraw_scheduler.redraw_count, 1);

    std::error_code error_code;
    std::filesystem::remove(log_path, error_code);
}

TEST(ModelRefreshTest, AppendFoldsNewlyPolledLinesAndRequestsRedraw)
{
    const auto log_path = make_temp_log_path();
    write_lines(log_path, "first line\nsecond line\n", std::ios::trunc);

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());

    AllProcessedSources processed_sources;
    RecordingRedrawScheduler redraw_scheduler;
    reload_processed_sources(tracked_sources, processed_sources, redraw_scheduler);
    ASSERT_EQ(processed_sources.total_line_count(), 2U);

    write_lines(log_path, "third line\n", std::ios::app);
    const auto first_new_line_index = tracked_sources.poll();
    ASSERT_TRUE(first_new_line_index.has_value());

    append_sources_delta_to_processed_sources(tracked_sources, *first_new_line_index, processed_sources, redraw_scheduler);

    EXPECT_EQ(processed_sources.total_line_count(), 3U);
    EXPECT_EQ(redraw_scheduler.redraw_count, 2);

    std::error_code error_code;
    std::filesystem::remove(log_path, error_code);
}

} // namespace slayerlog
