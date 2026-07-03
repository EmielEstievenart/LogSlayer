#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui_components/text_view_controller.hpp>

namespace slayerlog
{

class LogViewData;

// Mouse-driven text selection over the lines exposed by a LogViewData.
// Positions are model-space (line index + column). Every query that reads the
// underlying content expects the caller to already hold the data lock.
class LogViewSelection
{
public:
    /**
     * @brief Start a new selection, anchoring it at the given position.
     *
     * Discards any existing selection and marks a selection as in progress.
     *
     * @param position Model-space anchor position; clamped to the content bounds.
     * @param data Locked content the position is clamped against.
     */
    void begin(TextViewPosition position, const LogViewData& data);

    /**
     * @brief Move the focus end of an in-progress selection.
     *
     * Has no effect unless a selection was started with begin().
     *
     * @param position Model-space focus position; clamped to the content bounds.
     * @param data Locked content the position is clamped against.
     */
    void update(TextViewPosition position, const LogViewData& data);

    /**
     * @brief Finish the in-progress selection.
     *
     * Clears the in-progress flag and, when a final position is supplied, moves
     * the focus end to it. The resulting range remains available for querying.
     *
     * @param position Optional final focus position; clamped when present.
     * @param data Locked content the position is clamped against.
     */
    void end(std::optional<TextViewPosition> position, const LogViewData& data);

    /**
     * @brief Discard the current selection, leaving nothing selected.
     */
    void clear();

    /**
     * @brief Whether a selection drag is currently in progress.
     *
     * @return True between begin() and end().
     */
    [[nodiscard]] bool in_progress() const;

    /**
     * @brief Extract the selected text.
     *
     * Lines spanning the selection are joined with '\n'.
     *
     * @param data Locked content the selection refers to.
     * @return The selected text, or an empty string when nothing is selected.
     */
    [[nodiscard]] std::string text(const LogViewData& data) const;

    /**
     * @brief Build the highlight decorations covering the selection.
     *
     * Each decoration is an inverted-style range over a single visible line, in
     * model-space columns.
     *
     * @param data Locked content the selection refers to.
     * @return One decoration per selected line, or empty when nothing is selected.
     */
    [[nodiscard]] std::vector<TextViewRangeDecoration> decorations(const LogViewData& data) const;

private:
    [[nodiscard]] std::optional<std::pair<TextViewPosition, TextViewPosition>> bounds(const LogViewData& data) const;
    [[nodiscard]] TextViewPosition clamp(TextViewPosition position, const LogViewData& data) const;

    bool _in_progress = false;
    std::optional<TextViewPosition> _anchor;
    std::optional<TextViewPosition> _focus;
};

} // namespace slayerlog
