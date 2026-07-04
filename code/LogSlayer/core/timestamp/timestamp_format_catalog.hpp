#pragma once

#include <memory>
#include <string>
#include <vector>

namespace eestv
{
struct CompiledDateAndTimeParser;
}

namespace slayerlog
{

class TimestampFormatCatalog
{
public:
    struct Entry
    {
        std::string format;
        std::shared_ptr<const eestv::CompiledDateAndTimeParser> compiled_parser;
    };

    /// A configured format string that failed to compile, with the compiler's error message.
    struct RejectedFormat
    {
        std::string format;
        std::string error;
    };

    explicit TimestampFormatCatalog(std::vector<std::string> formats);
    ~TimestampFormatCatalog();

    TimestampFormatCatalog(const TimestampFormatCatalog&)                = default;
    TimestampFormatCatalog& operator=(const TimestampFormatCatalog&)     = default;
    TimestampFormatCatalog(TimestampFormatCatalog&&) noexcept            = default;
    TimestampFormatCatalog& operator=(TimestampFormatCatalog&&) noexcept = default;

    const std::vector<std::string>& formats() const;
    const std::vector<Entry>& entries() const;

    /// Formats rejected at construction because they failed to compile. The valid formats
    /// are unaffected; when every format is rejected the catalog falls back to the defaults.
    const std::vector<RejectedFormat>& rejected_formats() const;

private:
    void compile_and_add(const std::string& format);

    std::vector<std::string> _formats;
    std::vector<Entry> _entries;
    std::vector<RejectedFormat> _rejected_formats;
};

std::vector<std::string> default_timestamp_formats();
std::shared_ptr<const TimestampFormatCatalog> default_timestamp_format_catalog();
void set_default_timestamp_format_catalog(std::shared_ptr<const TimestampFormatCatalog> catalog);

} // namespace slayerlog
