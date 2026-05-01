#include "command_registrar.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <ftxui/component/screen_interactive.hpp>

#include "command_manager.hpp"
#include "command_palette_controller.hpp"
#include "debug_log.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "log_controller.hpp"
#include "log_source.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

std::string build_header_text(const std::vector<std::string>& labels)
{
    if (labels.empty())
    {
        return "No files opened (use open-file <path> or open-folder <path>)";
    }

    std::ostringstream output;
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        if (index > 0)
        {
            output << " | ";
        }

        output << labels[index] << ":" << (index + 1);
    }

    return output.str();
}

void reload_processed_sources(const AllTrackedSources& tracked_sources, std::string& header_text, AllProcessedSources& processed_sources, LogController& controller, ftxui::ScreenInteractive& screen)
{
    header_text = build_header_text(tracked_sources.source_labels());
    processed_sources.set_show_source_labels(tracked_sources.source_count() > 0);
    processed_sources.rebuild_from_sources(tracked_sources);
    controller.rebuild_view(processed_sources);
    (void)processed_sources.consume_column_width_growth();
    screen.PostEvent(ftxui::Event::Custom);
}

namespace
{

std::optional<int> parse_positive_line_number(std::string_view text)
{
    if (text.empty())
    {
        return std::nullopt;
    }

    const std::string line_text(text);
    std::size_t parsed_length = 0;
    int line_number           = 0;
    try
    {
        line_number = std::stoi(line_text, &parsed_length);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }

    if (parsed_length != line_text.size() || line_number <= 0)
    {
        return std::nullopt;
    }

    return line_number;
}

std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

std::optional<int> highest_shown_line_number(const AllProcessedSources& processed_sources, const LogController& controller)
{
    if (processed_sources.line_count() == 0)
    {
        return std::nullopt;
    }

    const int viewport_line_count = std::max(1, controller.text_view_controller().viewport_line_count());
    const int first_visible_line  = controller.text_view_controller().first_visible_line();
    const int last_visible_line   = std::min(processed_sources.line_count() - 1, first_visible_line + viewport_line_count - 1);
    return processed_sources.line_number_for_visible_line(VisibleLineIndex {last_visible_line});
}

CommandResult open_file_command(std::string_view file_path, AllTrackedSources& tracked_sources, std::string& header_text, AllProcessedSources& processed_sources, LogController& controller, ftxui::ScreenInteractive& screen)
{
    LogSource source;
    try
    {
        source = parse_log_source(file_path);
    }
    catch (const std::exception& ex)
    {
        return CommandResult {false, ex.what()};
    }

    const auto error = tracked_sources.open_source(source);
    if (error.has_value())
    {
        SLAYERLOG_LOG_ERROR("open-file failed file=" << file_path << " error=" << *error);
        return CommandResult {false, *error};
    }

    reload_processed_sources(tracked_sources, header_text, processed_sources, controller, screen);
    return CommandResult {true, "Opened file: " + source_display_path(source)};
}

CommandResult open_folder_command(std::string_view folder_path, AllTrackedSources& tracked_sources, std::string& header_text, AllProcessedSources& processed_sources, LogController& controller, ftxui::ScreenInteractive& screen)
{
    LogSource source;
    try
    {
        source = make_local_folder_source(folder_path);
    }
    catch (const std::exception& ex)
    {
        return CommandResult {false, ex.what()};
    }

    const auto error = tracked_sources.open_source(source);
    if (error.has_value())
    {
        SLAYERLOG_LOG_ERROR("open-folder failed folder=" << source_display_path(source) << " error=" << *error);
        return CommandResult {false, *error};
    }

    reload_processed_sources(tracked_sources, header_text, processed_sources, controller, screen);
    return CommandResult {true, "Opened folder: " + source_display_path(source)};
}

CommandResult export_visible_text_command(std::string_view file_path, const AllProcessedSources& processed_sources)
{
    const std::string trimmed_path = trim_text(file_path);
    if (trimmed_path.empty())
    {
        return CommandResult {false, "Usage: export-visible-text <path>"};
    }

    const std::filesystem::path output_path(trimmed_path);
    std::error_code error_code;
    if (output_path.has_parent_path() && !std::filesystem::exists(output_path.parent_path(), error_code))
    {
        if (error_code)
        {
            return CommandResult {false, "Could not access parent folder: " + output_path.parent_path().string()};
        }

        return CommandResult {false, "Parent folder does not exist: " + output_path.parent_path().string()};
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        return CommandResult {false, "Could not open file for writing: " + trimmed_path};
    }

    for (int line_index = 0; line_index < processed_sources.line_count(); ++line_index)
    {
        if (line_index > 0)
        {
            output << '\n';
        }

        output << processed_sources.rendered_line(line_index);
        if (!output)
        {
            return CommandResult {false, "Failed while writing file: " + trimmed_path};
        }
    }

    output.close();
    if (!output)
    {
        return CommandResult {false, "Failed while writing file: " + trimmed_path};
    }

    return CommandResult {true, "Exported " + std::to_string(processed_sources.line_count()) + " visible lines to " + trimmed_path};
}

CommandResult close_open_file_command(CommandPaletteController& command_palette_controller, AllTrackedSources& tracked_sources, std::string& header_text, AllProcessedSources& processed_sources, LogController& controller,
                                      ftxui::ScreenInteractive& screen)
{
    const auto labels = tracked_sources.source_labels();
    if (labels.empty())
    {
        return CommandResult {false, "No open files to close"};
    }

    command_palette_controller.open_close_open_file_picker(labels,
                                                           [&](std::size_t selected_index) -> CommandResult
                                                           {
                                                               std::string closed_label;
                                                               const auto error = tracked_sources.close_source(selected_index, &closed_label);
                                                               if (error.has_value())
                                                               {
                                                                   SLAYERLOG_LOG_ERROR("close-open-file failed selected_index=" << selected_index << " error=" << *error);
                                                                   return CommandResult {false, *error};
                                                               }

                                                               reload_processed_sources(tracked_sources, header_text, processed_sources, controller, screen);
                                                               return CommandResult {true, "Closed file: " + closed_label};
                                                           });

    return CommandResult {true, "Select a file to close", false};
}

std::vector<CommandPaletteModel::FilterPickerEntry> build_filter_picker_entries(const AllProcessedSources& processed_sources)
{
    std::vector<CommandPaletteModel::FilterPickerEntry> entries;
    for (const auto& filter : processed_sources.all_filters())
    {
        entries.push_back(CommandPaletteModel::FilterPickerEntry {filter.text, filter.include, filter.index, false});
    }

    return entries;
}

CommandResult delete_filters_command(CommandPaletteController& command_palette_controller, AllProcessedSources& processed_sources, LogController& controller)
{
    const auto filter_entries = build_filter_picker_entries(processed_sources);
    if (filter_entries.empty())
    {
        return CommandResult {false, "No filters to delete"};
    }

    command_palette_controller.open_delete_filters_picker(filter_entries,
                                                          [&](const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters) -> CommandResult
                                                          {
                                                              std::vector<AllProcessedSources::FilterSelection> filters_to_remove;
                                                              filters_to_remove.reserve(selected_filters.size());
                                                              for (const auto& filter : selected_filters)
                                                              {
                                                                  filters_to_remove.push_back(AllProcessedSources::FilterSelection {filter.include, filter.filter_index, filter.label});
                                                              }

                                                              if (!processed_sources.remove_filters(filters_to_remove))
                                                              {
                                                                  return CommandResult {false, "Failed to delete selected filters"};
                                                              }

                                                              controller.rebuild_view(processed_sources);
                                                              return CommandResult {true, "Deleted " + std::to_string(filters_to_remove.size()) + " filter" + (filters_to_remove.size() == 1 ? "" : "s")};
                                                          });

    return CommandResult {true, "Mark filters to delete and press Enter", false};
}

} // namespace

void register_commands(CommandManager& command_manager, AllProcessedSources& processed_sources, LogController& controller, CommandPaletteController& command_palette_controller, std::string& header_text, ftxui::ScreenInteractive& screen,
                       AllTrackedSources& tracked_sources)
{
    command_manager.register_command({"filter-in",
                                      "Show lines matching text or regex",
                                      "filter-in <text|re:regex>",
                                      {
                                          "Keep only matching lines visible.",
                                          "Use plain text for substring matching or prefix with re: for a regular expression.",
                                          "Example: filter-in auth",
                                          "Example: filter-in re:^(ERROR|WARN)",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         if (arguments.empty())
                                         {
                                             return CommandResult {false, "Usage: filter-in <text|re:regex>"};
                                         }

                                         try
                                         {
                                             processed_sources.add_include_filter(std::string(arguments));
                                         }
                                         catch (const std::invalid_argument& error)
                                         {
                                             return CommandResult {false, "Invalid filter-in pattern: " + std::string(error.what())};
                                         }

                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Added include filter: " + std::string(arguments)};
                                     });

    command_manager.register_command({"filter-out",
                                      "Hide lines matching text or regex",
                                      "filter-out <text|re:regex>",
                                      {
                                          "Hide matching lines while keeping everything else visible.",
                                          "Use plain text for substring matching or prefix with re: for a regular expression.",
                                          "Example: filter-out heartbeat",
                                          "Example: filter-out re:^DEBUG",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         if (arguments.empty())
                                         {
                                             return CommandResult {false, "Usage: filter-out <text|re:regex>"};
                                         }

                                         try
                                         {
                                             processed_sources.add_exclude_filter(std::string(arguments));
                                         }
                                         catch (const std::invalid_argument& error)
                                         {
                                             return CommandResult {false, "Invalid filter-out pattern: " + std::string(error.what())};
                                         }

                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Added exclude filter: " + std::string(arguments)};
                                     });

    command_manager.register_command({"reset-filters", "Clear all active filters", "reset-filters"},
                                     [&](std::string_view arguments)
                                     {
                                         if (!arguments.empty())
                                         {
                                             return CommandResult {false, "Usage: reset-filters"};
                                         }

                                         processed_sources.reset_filters();
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Cleared all filters"};
                                     });

    command_manager.register_command({"delete-filters",
                                      "Delete one or more active filters",
                                      "delete-filters",
                                      {
                                          "Open a picker containing every active filter.",
                                          "Use Up/Down to select a filter, Space to mark it, and Enter to delete the marked filters.",
                                          "Each filter is labeled with (in) or (out).",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         if (!trim_text(arguments).empty())
                                         {
                                             return CommandResult {false, "Usage: delete-filters"};
                                         }

                                         return delete_filters_command(command_palette_controller, processed_sources, controller);
                                     });

    command_manager.register_command({"clear-filters", "Alias for reset-filters", "clear-filters"},
                                     [&](std::string_view arguments)
                                     {
                                         if (!arguments.empty())
                                         {
                                             return CommandResult {false, "Usage: clear-filters"};
                                         }

                                         processed_sources.reset_filters();
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Cleared all filters"};
                                     });

    command_manager.register_command({"hide-columns",
                                      "Hide displayed columns in a range",
                                      "hide-columns <xx-yy>",
                                      {
                                          "Hide character columns inclusively across every rendered line.",
                                          "Example: hide-columns 25-80",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         const auto hidden_columns = parse_hidden_column_range(arguments);
                                         if (!hidden_columns.has_value())
                                         {
                                             return CommandResult {false, "Usage: hide-columns <xx-yy>"};
                                         }

                                         processed_sources.hide_columns(hidden_columns->start, hidden_columns->end);
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {
                                             true,
                                             "Hidden columns " + std::to_string(hidden_columns->start) + "-" + std::to_string(hidden_columns->end),
                                         };
                                     });

    command_manager.register_command({"reset-column-filter", "Clear the hidden column range", "reset-column-filter"},
                                     [&](std::string_view arguments)
                                     {
                                         if (!arguments.empty())
                                         {
                                             return CommandResult {false, "Usage: reset-column-filter"};
                                         }

                                         processed_sources.reset_hidden_columns();
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Cleared hidden column filter"};
                                     });

    command_manager.register_command({"clear-column-filters", "Alias for reset-column-filter", "clear-column-filters"},
                                     [&](std::string_view arguments)
                                     {
                                         if (!arguments.empty())
                                         {
                                             return CommandResult {false, "Usage: clear-column-filters"};
                                         }

                                         processed_sources.reset_hidden_columns();
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Cleared hidden column filter"};
                                     });

    command_manager.register_command({"show-original-time",
                                      "Show detected timestamp in original text",
                                      "show-original-time",
                                      {
                                          "When a timestamp is detected in a log line, keep it visible in the message text.",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         if (!trim_text(arguments).empty())
                                         {
                                             return CommandResult {false, "Usage: show-original-time"};
                                         }

                                         processed_sources.set_show_original_time(true);
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Showing original detected timestamps in messages"};
                                      });

    command_manager.register_command({"hide-original-time",
                                      "Hide detected timestamp in original text",
                                      "hide-original-time",
                                      {
                                          "When a timestamp is detected in a log line, remove it from the rendered message text.",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         if (!trim_text(arguments).empty())
                                         {
                                             return CommandResult {false, "Usage: hide-original-time"};
                                         }

                                         processed_sources.set_show_original_time(false);
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Hiding original detected timestamps in messages"};
                                      });

    command_manager.register_command({"show-identical-lines",
                                      "Show identical messages line-by-line",
                                      "show-identical-lines",
                                      {
                                          "Render every matching message row instead of collapsing identical rows.",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         if (!trim_text(arguments).empty())
                                         {
                                             return CommandResult {false, "Usage: show-identical-lines"};
                                         }

                                         processed_sources.set_hide_identical_lines(false);
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Showing identical messages"};
                                     });

    command_manager.register_command({"hide-identical-lines",
                                      "Hide repeated identical messages",
                                      "hide-identical-lines",
                                      {
                                          "Keep the first occurrence and show a summary row for identical messages above.",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         if (!trim_text(arguments).empty())
                                         {
                                             return CommandResult {false, "Usage: hide-identical-lines"};
                                         }

                                         processed_sources.set_hide_identical_lines(true);
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Hiding identical messages"};
                                     });

    command_manager.register_command({"open-file",
                                      "Open file and reload all tracked logs",
                                      "open-file <path>",
                                      {
                                          "Open a local file or an SSH-backed remote file and start tailing it.",
                                          "SSH sources must use ssh://user@host/absolute/path.log.",
                                          "Example: open-file logs/app.log",
                                          "Example: open-file ssh://user@example.com/var/log/app.log",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         const std::string file_path = trim_text(arguments);
                                         if (file_path.empty())
                                         {
                                             return CommandResult {false, "Usage: open-file <path>"};
                                         }

                                         return open_file_command(file_path, tracked_sources, header_text, processed_sources, controller, screen);
                                     });

    command_manager.register_command({"open-folder",
                                      "Open folder and reload all tracked logs",
                                      "open-folder <path>",
                                      {
                                          "Watch every regular file in a local folder and include new files as they appear.",
                                          "Folder watching also opens .zst files and reads them through the zstd watcher.",
                                          "Use this for log directories; SSH folders are not supported.",
                                          "Example: open-folder logs/archive",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         const std::string folder_path = trim_text(arguments);
                                         if (folder_path.empty())
                                         {
                                             return CommandResult {false, "Usage: open-folder <path>"};
                                         }

                                         return open_folder_command(folder_path, tracked_sources, header_text, processed_sources, controller, screen);
                                     });

    command_manager.register_command({"close-open-file",
                                      "Close one currently open file",
                                      "close-open-file",
                                      {
                                          "Opens a picker containing the currently tracked sources.",
                                          "Use Up/Down to select a source and Enter to close it.",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         if (!trim_text(arguments).empty())
                                         {
                                             return CommandResult {false, "Usage: close-open-file"};
                                         }

                                         return close_open_file_command(command_palette_controller, tracked_sources, header_text, processed_sources, controller, screen);
                                     });

    command_manager.register_command({"go-to-line",
                                      "Center the view on a line number",
                                      "go-to-line <line-number>",
                                      {
                                          "Jumps to a raw line number after all currently loaded sources are merged.",
                                          "The target line must still be visible after filters and cutoffs are applied.",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         const auto line_number = parse_positive_line_number(arguments);
                                         if (!line_number.has_value())
                                         {
                                             return CommandResult {false, "Usage: go-to-line <line-number>"};
                                         }

                                         if (*line_number > processed_sources.total_line_count())
                                         {
                                             return CommandResult {false, "Line " + std::to_string(*line_number) + " is out of range"};
                                         }

                                         if (!controller.go_to_line(processed_sources, *line_number))
                                         {
                                             return CommandResult {
                                                 false,
                                                 "Line " + std::to_string(*line_number) + " is hidden by current filters or line cutoff",
                                             };
                                         }

                                         return CommandResult {
                                             true,
                                             "Centered view on line " + std::to_string(*line_number),
                                         };
                                     });

    command_manager.register_command({"hide-before-line",
                                      "Hide all raw lines before a line number",
                                      "hide-before-line <line-number>",
                                      {
                                          "Use 1 to show everything again.",
                                          "Example: hide-before-line 5000",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         const auto line_number = parse_positive_line_number(arguments);
                                         if (!line_number.has_value())
                                         {
                                             return CommandResult {false, "Usage: hide-before-line <line-number>"};
                                         }

                                         processed_sources.hide_before_line_number(*line_number);
                                         controller.rebuild_view(processed_sources);
                                         if (*line_number == 1)
                                         {
                                             return CommandResult {true, "Showing all lines"};
                                         }

                                         return CommandResult {
                                             true,
                                             "Hidden all lines before line " + std::to_string(*line_number),
                                         };
                                     });

    command_manager.register_command({"hide-shown-lines", "Hide all currently shown lines", "hide-shown-lines"},
                                     [&](std::string_view arguments)
                                     {
                                         if (!trim_text(arguments).empty())
                                         {
                                             return CommandResult {false, "Usage: hide-shown-lines"};
                                         }

                                         const auto line_number = highest_shown_line_number(processed_sources, controller);
                                         if (!line_number.has_value())
                                         {
                                             return CommandResult {false, "No lines are currently shown"};
                                         }

                                         processed_sources.hide_before_line_number(*line_number + 1);
                                         controller.rebuild_view(processed_sources);
                                         return CommandResult {true, "Hidden all currently shown lines"};
                                     });

    command_manager.register_command({"export-visible-text",
                                      "Write visible rendered lines to a file",
                                      "export-visible-text <path>",
                                      {
                                          "Exports the currently visible rendered lines after filters and hidden columns are applied.",
                                          "This writes all visible lines in the model, not just the current viewport.",
                                          "Example: export-visible-text filtered.log",
                                      }},
                                     [&](std::string_view arguments) { return export_visible_text_command(arguments, processed_sources); });

    command_manager.register_command({"find",
                                      "Find lines matching text or regex",
                                      "find <text|re:regex>",
                                      {
                                          "Highlights matching lines and focuses the first visible match when possible.",
                                          "Use plain text for substring matching or prefix with re: for a regular expression.",
                                          "After find is active, use Right/Left to move between matches and Esc to clear it.",
                                          "Example: find timeout",
                                          "Example: find re:request_id=[0-9]+",
                                      }},
                                     [&](std::string_view arguments)
                                     {
                                         const std::string query = trim_text(arguments);
                                         if (query.empty())
                                         {
                                             return CommandResult {false, "Usage: find <text|re:regex>"};
                                         }

                                         bool focused_visible_match = false;
                                         try
                                         {
                                             focused_visible_match = controller.set_find_query(processed_sources, query);
                                         }
                                         catch (const std::invalid_argument& error)
                                         {
                                             return CommandResult {false, "Invalid find pattern: " + std::string(error.what())};
                                         }

                                         const int visible_matches = controller.visible_find_match_count(processed_sources);
                                         const int total_matches   = controller.total_find_match_count();
                                         if (focused_visible_match)
                                         {
                                             return CommandResult {
                                                 true,
                                                 "Find active: " + controller.find_query() + " (" + std::to_string(visible_matches) + " visible / " + std::to_string(total_matches) + " total)",
                                             };
                                         }

                                         return CommandResult {
                                             true,
                                             "Find active: " + controller.find_query() + " (0 visible / " + std::to_string(total_matches) + " total)",
                                         };
                                     });
}

} // namespace slayerlog
