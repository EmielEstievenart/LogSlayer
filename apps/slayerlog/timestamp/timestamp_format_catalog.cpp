#include "timestamp/timestamp_format_catalog.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

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

TimestampFormatCatalog::TimestampFormatCatalog(std::vector<std::string> formats) : _formats(sanitize_formats(std::move(formats)))
{
    eestv::TimestampParser parser;
    _entries.reserve(_formats.size());
    for (const auto& format : _formats)
    {
        _entries.push_back(Entry {format, std::make_shared<eestv::compiledDataAndTimeParser>(parser.CompileFormat(format))});
    }
}

TimestampFormatCatalog::~TimestampFormatCatalog() = default;

const std::vector<std::string>& TimestampFormatCatalog::formats() const
{
    return _formats;
}

const std::vector<TimestampFormatCatalog::Entry>& TimestampFormatCatalog::entries() const
{
    return _entries;
}

std::vector<std::string> default_timestamp_formats()
{
    return {
        "YYYY-MM-DDThh:mm:ss.ffffffZZZ",
        "YYYY-MM-DDThh:mm:ssZZZ",
        "YYYY-MM-DDThh:mm:ssZZ",
        "YYYY-MM-DDThh:mm:ssZ",
        "YYYY-MM-DDThh:mm:ss.f",
        "YYYY-MM-DDThh:mm:ss.fff",
        "YYYY-MM-DDThh:mm:ss",
        "[YYYY-MM-DDThh:mm:ss]",
        "YYYY-MM-DD hh:mm:ss",
        "[YYYY-MM-DD hh:mm:ss]",
        "YYYY-MM-DD hh:mm:ss.fff",
        "YYYY-MM-DD hh:mm:ss,fff",
        "DD-MMM-YYYY hh:mm:ss",
        "MMM DD hh:mm:ss",
        "DD/MMM/YYYY:hh:mm:ss ZZ",
        "YYYYMMDDThhmmssZ",
        "YYYYMMDDThhmmssZZ",
    };
}

std::shared_ptr<const TimestampFormatCatalog> default_timestamp_format_catalog()
{
    return mutable_default_catalog();
}

void set_default_timestamp_format_catalog(std::shared_ptr<const TimestampFormatCatalog> catalog)
{
    if (catalog == nullptr)
    {
        catalog = std::make_shared<const TimestampFormatCatalog>(default_timestamp_formats());
    }

    mutable_default_catalog() = std::move(catalog);
}

} // namespace slayerlog
