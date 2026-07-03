#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "log_types.hpp"
#include "log_view_data.hpp"
#include "search_pattern.hpp"

namespace slayerlog
{

// Incremental text search over the lines exposed by a LogViewData.
// Holds the compiled query, the set of matching line indices, and the active
// match. Subscribes to the data's update callback so matches stay in sync as
// new lines stream in.
class LogViewFindManager
{
public:
    /**
     * @brief Construct a find manager bound to a data source.
     *
     * Registers an update callback so the match set tracks streaming changes.
     *
     * @param data Data source to search; may be null, in which case the manager
     *             is inert.
     */
    explicit LogViewFindManager(std::shared_ptr<LogViewData> data);

    /**
     * @brief Unsubscribe from the data source's update callback.
     */
    ~LogViewFindManager();

    LogViewFindManager(const LogViewFindManager&)            = delete;
    LogViewFindManager& operator=(const LogViewFindManager&) = delete;

    /**
     * @brief Set the search query and recompute matches.
     *
     * An empty or whitespace-only query clears the active search.
     *
     * @param query Raw search text; trimmed before use.
     * @return True when the query is non-empty and a match was focused.
     */
    bool set_query(std::string query);

    /**
     * @brief Discard the current query and all matches.
     */
    void clear();

    /**
     * @brief Whether a search query is currently active.
     *
     * @return True while a non-empty query is set.
     */
    [[nodiscard]] bool active() const;

    /**
     * @brief The raw text of the active query.
     *
     * @return The query string, or empty when no search is active.
     */
    [[nodiscard]] const std::string& query() const;

    /**
     * @brief Number of lines matching the active query.
     *
     * @return The match count, or zero when no search is active.
     */
    [[nodiscard]] std::size_t match_count() const;

    /**
     * @brief Whether a line is one of the current matches.
     *
     * @param line_index Model-space line index.
     * @return True when the line matches the active query.
     */
    [[nodiscard]] bool line_matches(std::size_t line_index) const;

    /**
     * @brief Whether a line is the currently focused match.
     *
     * @param line_index Model-space line index.
     * @return True when the line is the active match.
     */
    [[nodiscard]] bool line_is_active_match(std::size_t line_index) const;

    /**
     * @brief The line index of the currently focused match.
     *
     * @return The active match line, or nullopt when none is focused.
     */
    [[nodiscard]] std::optional<std::size_t> active_match_line() const;

    /**
     * @brief Advance the active match to the next one, wrapping around.
     *
     * @return True when a match was focused.
     */
    bool go_to_next_match();

    /**
     * @brief Move the active match to the previous one, wrapping around.
     *
     * @return True when a match was focused.
     */
    bool go_to_previous_match();

    /**
     * @brief Take the pending focus request raised by the last match change.
     *
     * The request is cleared by this call so the view scrolls only once.
     *
     * @return The line to scroll into view, or nullopt when none is pending.
     */
    std::optional<std::size_t> consume_pending_focus_line();

private:
    void on_data_changed(VisibleLineIndex first_changed_line);
    void rebuild_matches();
    void rebuild_matches_from(std::size_t first_changed_line);
    [[nodiscard]] bool row_matches_query(std::size_t line_index) const;
    bool focus_match_at(std::size_t match_position);

    std::shared_ptr<LogViewData> _data;
    LogViewData::CallbackId _update_callback_id = 0;
    std::string _query;
    std::optional<SearchPattern> _pattern;
    std::vector<std::size_t> _matching_lines;
    std::optional<std::size_t> _active_match_line;
    std::optional<std::size_t> _pending_focus_line;
};

} // namespace slayerlog
