#include "log_entry_presentation.hpp"

#include "tracked_source_base.hpp"

namespace slayerlog
{

std::string presented_prefix(const LogEntry& entry)
{
    // metadata.source can be null for manually-built tests or transient entries.
    if (entry.metadata.source == nullptr)
    {
        return {};
    }

    return entry.metadata.source->mnemonic_prefix();
}

std::string presented_text(const LogEntry& entry)
{
    return presented_prefix(entry) + entry.text;
}

} // namespace slayerlog
