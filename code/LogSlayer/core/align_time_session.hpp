#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tracked_sources/log_entry_presentation.hpp"
#include "tracked_sources/log_line.hpp"
#include "timestamp/log_timestamp.hpp"

namespace slayerlog
{

class AllProcessedSources;

/// Transient, UI-agnostic model that drives the dual-pane "align time" workflow for
/// one source. It snapshots the currently displayed entries, splits them into a fixed
/// backdrop (every other source, post-filter) and the aligning source's lines, and
/// previews a candidate timestamp offset WITHOUT mutating the real model: each nudge
/// re-merges the aligning lines (shifted by the preview offset) into the fixed backdrop,
/// so both panes share a single row ordering. Nothing is applied until the caller reads
/// preview_offset() on commit; cancel just discards the session.
///
/// Phases:
///   SelectRight - choose the single aligning-source line to move (right pane).
///   SelectLeft  - choose one or two reference lines from the other sources (left pane).
///   Nudge       - the coarse offset is auto-applied (snap the chosen line onto the lone
///                 reference, or onto the midpoint of two references); arrow nudges then
///                 fine-tune the preview offset by a fixed step.
class AlignTimeSession
{
public:
    /// One rung of the nudge-granularity ladder: the offset a single arrow-key nudge
    /// applies, plus a compact label for the panel ("1 ms", "100 \xc2\xb5s", ...).
    struct NudgeStep
    {
        LogTimestampOffset offset;
        std::string_view label;
    };

    /// Logarithmic ladder of nudge granularities, finest -> coarsest, each a decade apart.
    /// Left/Right during the Nudge phase step along it; Up/Down nudge by the current rung.
    /// (The micro sign is spelled with an explicit UTF-8 byte escape so the source stays
    /// correct regardless of the compiler's source/execution charset.)
    static constexpr std::array<NudgeStep, 6> kNudgeSteps {{
        {{0, 1'000}, "1 \xc2\xb5s"},
        {{0, 10'000}, "10 \xc2\xb5s"},
        {{0, 100'000}, "100 \xc2\xb5s"},
        {{0, 1'000'000}, "1 ms"},
        {{0, 10'000'000}, "10 ms"},
        {{0, 100'000'000}, "100 ms"},
    }};

    /// Default granularity: 1 ms (index into kNudgeSteps).
    static constexpr std::size_t kDefaultStepIndex = 3;

    enum class Phase
    {
        SelectRight,
        SelectLeft,
        Nudge,
    };

    enum class RowKind
    {
        Backdrop,
        Aligning,
    };

    /// Snapshots @p processed_sources for the source at @p aligning_source_index.
    /// The caller must hold the model mutex for the duration of this constructor.
    AlignTimeSession(const AllProcessedSources& processed_sources, std::size_t aligning_source_index);

    [[nodiscard]] bool ready() const;
    [[nodiscard]] const std::string& status_text() const;
    [[nodiscard]] bool status_is_error() const;
    [[nodiscard]] std::size_t aligning_source_index() const;
    [[nodiscard]] const std::string& aligning_source_label() const;

    // --- Rendering surface for the two pane adapters ---
    [[nodiscard]] std::size_t row_count() const;
    [[nodiscard]] RowKind row_kind(std::size_t row) const;
    [[nodiscard]] std::string render_row(std::size_t row) const;
    [[nodiscard]] int widest_row_width() const;

    // --- Phase / selection state (for the controller + highlighting) ---
    [[nodiscard]] Phase phase() const;
    /// The active line: the cursor row while selecting, or the moving line during Nudge.
    [[nodiscard]] std::optional<int> cursor_row() const;
    [[nodiscard]] std::optional<int> right_selected_row() const;
    [[nodiscard]] std::vector<int> left_selected_rows() const;
    [[nodiscard]] LogTimestampOffset preview_offset() const;
    /// The offset a single nudge currently applies, and its compact label for the panel.
    [[nodiscard]] LogTimestampOffset current_step() const;
    [[nodiscard]] std::string_view current_step_label() const;
    [[nodiscard]] bool can_commit() const;

    // --- Interaction (driven by the controller) ---
    /// Move the cursor by @p delta selectable rows of the active phase's kind.
    void move_cursor(int delta);
    /// In SelectLeft, toggle the cursor row in/out of the (<=2) reference selection.
    void toggle_left_selection();
    /// Confirm the current step: SelectRight picks the cursor line and advances to
    /// SelectLeft; SelectLeft (with >=1 reference) applies the coarse offset and advances
    /// to Nudge. Returns true when the phase advanced.
    bool advance();
    /// In Nudge, shift the preview offset by @p steps of the current granularity (positive = later).
    void nudge(int steps);
    /// In Nudge, move one rung along the granularity ladder: @p direction > 0 makes the
    /// step coarser (up to 100 ms), < 0 makes it finer (down to 1 \xc2\xb5s). Clamped at both ends.
    void change_step(int direction);
    /// Step back one phase (Nudge -> SelectLeft -> SelectRight). Returns false at SelectRight.
    bool step_back();

private:
    struct AligningEntry
    {
        std::shared_ptr<LogEntry> entry;
        std::optional<LogTimestamp> base_timestamp;
    };

    void set_status(std::string message, bool is_error = false);
    void enter_phase(Phase phase);
    [[nodiscard]] std::optional<int> first_row_of_kind(RowKind kind) const;
    void apply_preview_offset_to_aligning();
    void rebuild_merge();
    [[nodiscard]] std::optional<int> row_of_entry(const std::shared_ptr<LogEntry>& entry) const;
    [[nodiscard]] bool compute_coarse_offset();

    std::size_t _aligning_source_index = 0;
    std::string _aligning_source_label;
    bool _ready = false;
    std::string _status;
    bool _status_is_error = false;

    std::vector<std::shared_ptr<LogEntry>> _backdrop;
    std::vector<AligningEntry> _aligning;
    std::vector<std::shared_ptr<LogEntry>> _merged;

    LogEntryColumnWidths _column_widths;
    bool _show_original_time = false;
    int _widest_row_width    = 0;

    Phase _phase    = Phase::SelectRight;
    int _cursor_row = 0;
    std::shared_ptr<LogEntry> _right_selection;
    std::optional<LogTimestamp> _right_base;
    std::vector<std::shared_ptr<LogEntry>> _left_selections;
    LogTimestampOffset _preview_offset {0, 0};
    std::size_t _step_index = kDefaultStepIndex;
};

} // namespace slayerlog
