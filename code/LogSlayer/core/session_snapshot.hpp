#pragma once

#include <string>
#include <vector>

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;
class LogViewService;

/**
 * @brief Serializes the current session as the command list that rebuilds it.
 *
 * The result is a snapshot of state, not a history: opens come first (local
 * paths absolutized so replay is independent of the working directory), then
 * per-source timestamp formats and offsets, filters, hidden columns, the
 * display toggles (always emitted so replay is immune to default changes), the
 * hide-before cutoff, and finally the view position as a go-to-line on the
 * line at the center of the viewport (skipped when @p log_view is null or has
 * no viewport yet).
 *
 * Every line is executable by CommandManager::execute, which is what --cmd,
 * saved configs, and exported scripts all feed into.
 */
std::vector<std::string> serialize_session_commands(const AllTrackedSources& tracked_sources, const AllProcessedSources& processed_sources, const LogViewService* log_view);

} // namespace slayerlog
