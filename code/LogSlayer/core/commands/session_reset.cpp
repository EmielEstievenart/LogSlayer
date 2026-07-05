#include "commands/session_reset.hpp"

#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

std::size_t reset_session_state(const CommandContext& context)
{
    const std::size_t closed_count = context.tracked_sources.source_count();
    while (context.tracked_sources.source_count() > 0)
    {
        context.tracked_sources.close_source(context.tracked_sources.source_count() - 1);
    }

    context.processed_sources.reset();
    context.log_view.reload(context.tracked_sources, context.processed_sources);
    return closed_count;
}

} // namespace slayerlog
