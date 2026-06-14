#pragma once

#include <memory>
#include <string>
#include <vector>

namespace eestv
{
struct compiledDataAndTimeParser;
}

namespace slayerlog
{

class TimestampFormatCatalog
{
public:
    struct Entry
    {
        std::string format;
        std::shared_ptr<eestv::compiledDataAndTimeParser> compiled_parser;
    };

    explicit TimestampFormatCatalog(std::vector<std::string> formats);
    ~TimestampFormatCatalog();

    TimestampFormatCatalog(const TimestampFormatCatalog&)                = default;
    TimestampFormatCatalog& operator=(const TimestampFormatCatalog&)     = default;
    TimestampFormatCatalog(TimestampFormatCatalog&&) noexcept            = default;
    TimestampFormatCatalog& operator=(TimestampFormatCatalog&&) noexcept = default;

    const std::vector<std::string>& formats() const;
    const std::vector<Entry>& entries() const;

private:
    std::vector<std::string> _formats;
    std::vector<Entry> _entries;
};

std::vector<std::string> default_timestamp_formats();
std::shared_ptr<const TimestampFormatCatalog> default_timestamp_format_catalog();
void set_default_timestamp_format_catalog(std::shared_ptr<const TimestampFormatCatalog> catalog);

} // namespace slayerlog
