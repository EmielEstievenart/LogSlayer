#include "timestamp/timestamp_format_catalog.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "debug_log.hpp"
#include "eestv/timestamp/timestamp_parser.hpp"

namespace slayerlog
{

namespace
{

std::vector<std::string> sanitize_formats(std::vector<std::string> formats)
{
    formats.erase(std::remove_if(formats.begin(), formats.end(), [](const std::string& format) { return format.empty(); }), formats.end());
    if (formats.empty())
    {
        return default_timestamp_formats();
    }

    return formats;
}

std::shared_ptr<const TimestampFormatCatalog>& mutable_default_catalog()
{
    static auto catalog = std::make_shared<const TimestampFormatCatalog>(default_timestamp_formats());
    return catalog;
}

} // namespace

TimestampFormatCatalog::TimestampFormatCatalog(std::vector<std::string> formats)
{
    const auto candidates = sanitize_formats(std::move(formats));
    _formats.reserve(candidates.size());
    _entries.reserve(candidates.size());
    for (const auto& format : candidates)
    {
        compile_and_add(format);
    }

    if (_entries.empty())
    {
        // Every configured format was invalid. Fall back to the defaults so timestamp
        // detection keeps working; rejected_formats() still reports what was wrong.
        for (const auto& format : default_timestamp_formats())
        {
            compile_and_add(format);
        }
    }
}

TimestampFormatCatalog::~TimestampFormatCatalog() = default;

void TimestampFormatCatalog::compile_and_add(const std::string& format)
{
    auto compiled = eestv::TimestampParser::CompileFormat(format);
    if (!compiled.valid())
    {
        SLAYERLOG_LOG_WARNING("Rejected timestamp format: " << compiled.error);
        _rejected_formats.push_back(RejectedFormat {format, std::move(compiled.error)});
        return;
    }

    _formats.push_back(format);
    _entries.push_back(Entry {format, std::make_shared<const eestv::CompiledDateAndTimeParser>(std::move(compiled))});
}

const std::vector<std::string>& TimestampFormatCatalog::formats() const
{
    return _formats;
}

const std::vector<TimestampFormatCatalog::Entry>& TimestampFormatCatalog::entries() const
{
    return _entries;
}

const std::vector<TimestampFormatCatalog::RejectedFormat>& TimestampFormatCatalog::rejected_formats() const
{
    return _rejected_formats;
}

std::vector<std::string> default_timestamp_formats()
{
    // Fractions use f* (any number of digits) so a shorter fixed-width fraction format can
    // never win as a prefix match and silently drop a trailing timezone.
    return {
        "YYYY-MM-DDThh:mm:ss.f*ZZZ", "YYYY-MM-DDThh:mm:ss.f*ZZ", "YYYY-MM-DDThh:mm:ss.f*Z", "YYYY-MM-DDThh:mm:ss.f*",  "YYYY-MM-DDThh:mm:ssZZZ", "YYYY-MM-DDThh:mm:ssZZ",
        "YYYY-MM-DDThh:mm:ssZ",      "YYYY-MM-DDThh:mm:ss",      "[YYYY-MM-DDThh:mm:ss]",   "YYYY-MM-DD hh:mm:ss.f*",  "YYYY-MM-DD hh:mm:ss,f*", "YYYY-MM-DD hh:mm:ss",
        "[YYYY-MM-DD hh:mm:ss]",     "DD-MMM-YYYY hh:mm:ss",     "MMM D* hh:mm:ss",         "DD/MMM/YYYY:hh:mm:ss ZZ", "YYYYMMDDThhmmssZ",       "YYYYMMDDThhmmssZZ",
    };
}

std::shared_ptr<const TimestampFormatCatalog> default_timestamp_format_catalog()
{
    return mutable_default_catalog();
}

void set_default_timestamp_format_catalog(std::shared_ptr<const TimestampFormatCatalog> catalog)
{
    // Not synchronized: main() installs the settings-backed catalog once during startup,
    // before the watcher thread and any background open tasks exist.
    if (catalog == nullptr)
    {
        catalog = std::make_shared<const TimestampFormatCatalog>(default_timestamp_formats());
    }

    mutable_default_catalog() = std::move(catalog);
}

} // namespace slayerlog
