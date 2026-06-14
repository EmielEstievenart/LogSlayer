#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "align_time_session.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "timestamp/log_timestamp.hpp"

namespace slayerlog
{

namespace
{

using Phase   = AlignTimeSession::Phase;
using RowKind = AlignTimeSession::RowKind;

LogTimestamp ts(unsigned hour, unsigned minute, unsigned second, unsigned nanosecond = 0)
{
    return *make_log_timestamp_utc(2026, 4, 1, hour, minute, second, nanosecond);
}

LogEntry entry(std::size_t source_index, std::string source_label, std::string text, LogTimestamp timestamp)
{
    LogEntry log_entry;
    log_entry.metadata.source_index = source_index;
    log_entry.metadata.source_label = std::move(source_label);
    log_entry.text                  = std::move(text);
    log_entry.metadata.timestamp    = timestamp;
    return log_entry;
}

// Source 0 "a.log" (backdrop): 10:00:00 / :10 / :20.
// Source 1 "b.log" (aligning): 09:00:00 / :05 (one hour earlier).
// AllProcessedSources is non-copyable (it owns a mutex), so populate by reference.
void populate_two_source_model(AllProcessedSources& processed_sources)
{
    processed_sources.append_lines(std::vector<LogEntry> {
        entry(0, "a.log", "a0", ts(10, 0, 0)),
        entry(0, "a.log", "a10", ts(10, 0, 10)),
        entry(0, "a.log", "a20", ts(10, 0, 20)),
        entry(1, "b.log", "b0", ts(9, 0, 0)),
        entry(1, "b.log", "b5", ts(9, 0, 5)),
    });
}

LogEntry offset_entry(std::size_t source_index, std::string source_label, std::string text, LogTimestamp timestamp, LogTimestamp existing_offset_timestamp)
{
    LogEntry log_entry                  = entry(source_index, std::move(source_label), std::move(text), timestamp);
    log_entry.metadata.offset_timestamp = existing_offset_timestamp;
    return log_entry;
}

int count_rows_of_kind(const AlignTimeSession& session, RowKind kind)
{
    int count = 0;
    for (std::size_t row = 0; row < session.row_count(); ++row)
    {
        if (session.row_kind(row) == kind)
        {
            ++count;
        }
    }

    return count;
}

} // namespace

TEST(AlignTimeSessionTest, PartitionsBackdropAndAligningRows)
{
    AllProcessedSources processed_sources;
    populate_two_source_model(processed_sources);
    AlignTimeSession session(processed_sources, 1);

    EXPECT_TRUE(session.ready());
    EXPECT_EQ(5u, session.row_count());
    EXPECT_EQ(2, count_rows_of_kind(session, RowKind::Aligning));
    EXPECT_EQ(3, count_rows_of_kind(session, RowKind::Backdrop));
    EXPECT_EQ("b.log", session.aligning_source_label());
    EXPECT_EQ(Phase::SelectRight, session.phase());
}

TEST(AlignTimeSessionTest, NotReadyWhenAligningSourceHasNoLines)
{
    AllProcessedSources processed_sources;
    processed_sources.append_lines(std::vector<LogEntry> {entry(0, "a.log", "a0", ts(10, 0, 0))});

    AlignTimeSession session(processed_sources, 1);
    EXPECT_FALSE(session.ready());
    EXPECT_TRUE(session.status_is_error());
}

TEST(AlignTimeSessionTest, NotReadyWhenNoBackdropLines)
{
    AllProcessedSources processed_sources;
    processed_sources.append_lines(std::vector<LogEntry> {entry(1, "b.log", "b0", ts(9, 0, 0))});

    AlignTimeSession session(processed_sources, 1);
    EXPECT_FALSE(session.ready());
    EXPECT_TRUE(session.status_is_error());
}

TEST(AlignTimeSessionTest, CursorStaysOnSelectableRowsPerPhase)
{
    AllProcessedSources processed_sources;
    populate_two_source_model(processed_sources);
    AlignTimeSession session(processed_sources, 1);

    // SelectRight: cursor only lands on aligning rows (rows 0 and 1 initially).
    ASSERT_TRUE(session.cursor_row().has_value());
    EXPECT_EQ(RowKind::Aligning, session.row_kind(static_cast<std::size_t>(*session.cursor_row())));
    session.move_cursor(5); // clamps to the last aligning row
    EXPECT_EQ(RowKind::Aligning, session.row_kind(static_cast<std::size_t>(*session.cursor_row())));

    // Confirm the right line, then SelectLeft cursor only lands on backdrop rows.
    EXPECT_TRUE(session.advance());
    EXPECT_EQ(Phase::SelectLeft, session.phase());
    ASSERT_TRUE(session.cursor_row().has_value());
    EXPECT_EQ(RowKind::Backdrop, session.row_kind(static_cast<std::size_t>(*session.cursor_row())));
}

TEST(AlignTimeSessionTest, CoarseSnapAlignsRightLineOntoSingleReference)
{
    AllProcessedSources processed_sources;
    populate_two_source_model(processed_sources);
    AlignTimeSession session(processed_sources, 1);

    // Right line = b0 (row 0, base 09:00:00).
    EXPECT_TRUE(session.advance());

    // Move to the a10 backdrop line (10:00:10) and select it as the reference.
    session.move_cursor(1); // a0 -> a10
    session.toggle_left_selection();
    EXPECT_TRUE(session.advance());

    EXPECT_EQ(Phase::Nudge, session.phase());

    // offset_between(09:00:00, 10:00:10) == +1h10s == 3610s.
    const auto offset = session.preview_offset();
    EXPECT_EQ(3610, offset.seconds);
    EXPECT_EQ(0, offset.nanosecond);
}

TEST(AlignTimeSessionTest, CoarseSnapUsesMidpointOfTwoReferences)
{
    AllProcessedSources processed_sources;
    populate_two_source_model(processed_sources);
    AlignTimeSession session(processed_sources, 1);

    EXPECT_TRUE(session.advance()); // right = b0 (09:00:00)

    // Select a0 (10:00:00) and a20 (10:00:20): midpoint is 10:00:10.
    session.toggle_left_selection(); // a0 at cursor
    session.move_cursor(2);          // a0 -> a20
    session.toggle_left_selection();
    EXPECT_TRUE(session.advance());

    const auto offset = session.preview_offset();
    EXPECT_EQ(3610, offset.seconds);
    EXPECT_EQ(0, offset.nanosecond);
}

TEST(AlignTimeSessionTest, NudgeShiftsPreviewByFixedStepAndMovesTheLine)
{
    AllProcessedSources processed_sources;
    populate_two_source_model(processed_sources);
    AlignTimeSession session(processed_sources, 1);

    EXPECT_TRUE(session.advance()); // right = b0
    session.move_cursor(1);         // reference = a10 (10:00:10)
    session.toggle_left_selection();
    EXPECT_TRUE(session.advance()); // -> Nudge, preview = 3610s

    // After coarse snap b0 sits at 10:00:10, just after a10 (ties resolve to the lower
    // source index). Merged order: a0, a10, b0, b5, a20.
    ASSERT_TRUE(session.right_selected_row().has_value());
    EXPECT_EQ(2, *session.right_selected_row());

    // One step later (+100 ms).
    session.nudge(1);
    EXPECT_EQ(3610, session.preview_offset().seconds);
    EXPECT_EQ(100'000'000, session.preview_offset().nanosecond);

    // Back to exactly 3610s, then one step earlier: b0 -> 10:00:09.9, now before a10.
    session.nudge(-1);
    session.nudge(-1);
    EXPECT_EQ(3609, session.preview_offset().seconds);
    EXPECT_EQ(900'000'000, session.preview_offset().nanosecond);
    ASSERT_TRUE(session.right_selected_row().has_value());
    EXPECT_EQ(1, *session.right_selected_row()); // the moving line followed its entry up
}

TEST(AlignTimeSessionTest, StepBackFromNudgeResetsPreviewAndPhase)
{
    AllProcessedSources processed_sources;
    populate_two_source_model(processed_sources);
    AlignTimeSession session(processed_sources, 1);

    EXPECT_TRUE(session.advance());
    session.move_cursor(1);
    session.toggle_left_selection();
    EXPECT_TRUE(session.advance()); // Nudge
    session.nudge(3);

    EXPECT_TRUE(session.step_back());
    EXPECT_EQ(Phase::SelectLeft, session.phase());

    EXPECT_TRUE(session.step_back());
    EXPECT_EQ(Phase::SelectRight, session.phase());
    EXPECT_EQ(0, session.preview_offset().seconds);
    EXPECT_EQ(0, session.preview_offset().nanosecond);

    EXPECT_FALSE(session.step_back()); // already at the first phase
}

TEST(AlignTimeSessionTest, CoarseSnapProducesIncrementalDeltaWhenSourceAlreadyOffset)
{
    // The aligning source already carries a +30 min offset (effective time 09:30:00).
    // The preview offset must be the DELTA to add on top of that existing offset, because
    // commit applies it via adjust_source_timestamp_offset (which adds, never resets).
    AllProcessedSources processed_sources;
    processed_sources.append_lines(std::vector<LogEntry> {
        entry(0, "a.log", "a10", ts(10, 0, 10)),
        offset_entry(1, "b.log", "b0", ts(9, 0, 0), ts(9, 30, 0)),
    });

    AlignTimeSession session(processed_sources, 1);
    ASSERT_TRUE(session.ready());

    EXPECT_TRUE(session.advance());  // right = b0 (effective 09:30:00)
    session.toggle_left_selection(); // reference = a10 (10:00:10)
    EXPECT_TRUE(session.advance());  // -> Nudge

    // offset_between(09:30:00, 10:00:10) == +30 min 10 s == 1810 s.
    const auto offset = session.preview_offset();
    EXPECT_EQ(1810, offset.seconds);
    EXPECT_EQ(0, offset.nanosecond);
}

TEST(AlignTimeSessionTest, CannotCommitBeforeNudgePhase)
{
    AllProcessedSources processed_sources;
    populate_two_source_model(processed_sources);
    AlignTimeSession session(processed_sources, 1);

    EXPECT_FALSE(session.can_commit());
    EXPECT_TRUE(session.advance());
    EXPECT_FALSE(session.can_commit());
    session.toggle_left_selection();
    EXPECT_TRUE(session.advance());
    EXPECT_TRUE(session.can_commit());
}

} // namespace slayerlog
