#pragma once

#include <string>

#include "log_view_service.hpp"

namespace slayerlog
{

/// LogViewService that does nothing. Lets commands be constructed without a
/// live view, e.g. to register the palette commands and print --help before
/// any UI exists.
class NullLogViewService final : public LogViewService
{
public:
    void rebuild_view(const AllProcessedSources& /*processed_sources*/) override { }
    void reload(const AllTrackedSources& /*tracked_sources*/, AllProcessedSources& /*processed_sources*/) override { }
    bool go_to_line(const AllProcessedSources& /*processed_sources*/, int /*line_number*/) override { return false; }
    int first_visible_line() const override { return 0; }
    int viewport_line_count() const override { return 0; }
    bool set_find_query(AllProcessedSources& /*processed_sources*/, std::string /*query*/) override { return false; }
    int total_find_match_count() const override { return 0; }
    int visible_find_match_count(const AllProcessedSources& /*processed_sources*/) const override { return 0; }
    const std::string& find_query() const override { return _empty_query; }
    void start_time_alignment(TimeAlignmentApplyCallback /*apply*/) override { }

private:
    std::string _empty_query;
};

} // namespace slayerlog
