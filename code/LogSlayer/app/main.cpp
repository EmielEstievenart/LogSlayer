#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "command_line_parser.hpp"
#include "command_manager.hpp"
#include "command_registrar.hpp"
#include "debug_log.hpp"
#include "log_view2_find_manager.hpp"
#include "null_log_view_service.hpp"
#include "run_gui.hpp"
#include "run_tui.hpp"
#include "settings_store.hpp"
#include "timestamp/timestamp_format_catalog.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/tracked_source_factory.hpp"

namespace
{

constexpr std::string_view timestamp_formats_section = "timestamp_formats";
constexpr std::string_view timestamp_format_key      = "format";

void initialize_debug_log(int argc, char** argv)
{
    slayerlog::debug_log::initialize(argc > 0 ? argv[0] : nullptr);
    SLAYERLOG_LOG_INFO("Debug log initialized at " << slayerlog::debug_log::log_file_path().string());
}

bool load_settings(slayerlog::SettingsStore& settings_store, std::string& settings_error_message)
{
    if (settings_store.load(settings_error_message))
    {
        return true;
    }

    SLAYERLOG_LOG_ERROR("Failed to load settings from " << settings_store.file_path() << ": " << settings_error_message << "; settings saves are disabled for this run");
    settings_error_message.clear();
    return false;
}

std::vector<std::string> load_timestamp_formats(slayerlog::SettingsStore& settings_store, bool settings_loaded, std::string& settings_error_message)
{
    std::vector<std::string> timestamp_formats = slayerlog::default_timestamp_formats();
    if (!settings_loaded)
    {
        SLAYERLOG_LOG_WARNING("Using built-in timestamp formats because settings failed to load from " << settings_store.file_path());
        return timestamp_formats;
    }

    if (!settings_store.ensure_default_values(timestamp_formats_section, timestamp_format_key, timestamp_formats, settings_error_message))
    {
        SLAYERLOG_LOG_WARNING("Failed to seed timestamp formats in settings file " << settings_store.file_path() << ": " << settings_error_message);
        settings_error_message.clear();
    }

    return settings_store.ini().values(timestamp_formats_section, timestamp_format_key);
}

std::shared_ptr<const slayerlog::TimestampFormatCatalog> configure_timestamp_formats(const std::vector<std::string>& timestamp_formats)
{
    auto timestamp_catalog = std::make_shared<const slayerlog::TimestampFormatCatalog>(timestamp_formats);
    slayerlog::set_default_timestamp_format_catalog(timestamp_catalog);
    return timestamp_catalog;
}

bool open_configured_sources(const slayerlog::Config& config, slayerlog::AllTrackedSources& tracked_sources)
{
    SLAYERLOG_LOG_INFO("Starting slayerlog poll_interval_ms=" << config.poll_interval_ms << " configured_sources=" << config.file_paths.size());
    for (const auto& file_path : config.file_paths)
    {
        slayerlog::LogSource source;
        try
        {
            source = slayerlog::parse_log_source(file_path);
        }
        catch (const std::exception& ex)
        {
            SLAYERLOG_LOG_ERROR("Initial source parse failed file=" << file_path << " error=" << ex.what());
            std::cerr << ex.what() << '\n';
            return false;
        }

        const auto error = open_source(tracked_sources, source);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("Initial source open failed file=" << file_path << " error=" << *error);
            std::cerr << *error << '\n';
            return false;
        }
    }

    return true;
}

/// Print --help and return true when requested. Registers the command set over
/// throwaway null/inert services so no UI (FTXUI or wx) has to be constructed
/// just to enumerate command descriptors.
bool handle_help_request(const slayerlog::Config& config, slayerlog::AllProcessedSources& processed_sources, slayerlog::AllTrackedSources& tracked_sources, const slayerlog::SettingsStore& settings_store)
{
    if (!config.show_help)
    {
        return false;
    }

    slayerlog::NullLogViewService null_log_view;
    slayerlog::LogView2FindManager find_manager(nullptr);
    slayerlog::CommandManager command_manager;
    slayerlog::register_log_view2_commands(command_manager, {processed_sources, null_log_view, tracked_sources, {}, nullptr, nullptr, settings_store.file_path()}, find_manager);
    std::cout << slayerlog::build_help_text(command_manager);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    initialize_debug_log(argc, argv);

    slayerlog::Config config;
    try
    {
        config = slayerlog::parse_command_line(argc, argv);
    }
    catch (const std::exception&)
    {
        // parse_command_line already printed the error and usage to stderr.
        return 2;
    }

    slayerlog::SettingsStore settings_store(slayerlog::default_settings_file_path());
    std::string settings_error_message;
    const bool settings_loaded = load_settings(settings_store, settings_error_message);

    const auto timestamp_formats = load_timestamp_formats(settings_store, settings_loaded, settings_error_message);
    auto timestamp_catalog       = configure_timestamp_formats(timestamp_formats);
    slayerlog::AllTrackedSources tracked_sources(timestamp_catalog);

    if (!open_configured_sources(config, tracked_sources))
    {
        return 1;
    }

    std::mutex model_mutex;
    slayerlog::AllProcessedSources processed_sources;

    if (handle_help_request(config, processed_sources, tracked_sources, settings_store))
    {
        return 0;
    }

    if (config.ui == slayerlog::UiKind::Gui)
    {
        return slayerlog::run_gui(argc, argv, config, settings_store, settings_loaded, tracked_sources, model_mutex, processed_sources);
    }

    return slayerlog::run_tui(config, settings_store, settings_loaded, tracked_sources, model_mutex, processed_sources);
}
