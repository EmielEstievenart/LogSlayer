#pragma once

#include <cstddef>

#include "command_context.hpp"

namespace slayerlog
{

/**
 * @brief Closes every source and clears all processed-view state.
 *
 * Filters, hidden columns, the hide-before cutoff, pause, and both display
 * toggles return to their defaults, and the view is reloaded. This is the
 * "replace everything" step used by reset-session and load-config.
 *
 * @return The number of sources that were closed.
 */
std::size_t reset_session_state(const CommandContext& context);

} // namespace slayerlog
