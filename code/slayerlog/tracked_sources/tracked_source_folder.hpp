#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "notifications/notification.hpp"
#include "tracked_source_base.hpp"
#include "tracked_source_file.hpp"

namespace slayerlog
{

class TrackedSourceFolder : public TrackedSourceBase
{
public:
    TrackedSourceFolder(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog(), Notifier notifier = {});

    bool poll() override;
    void set_timestamp_format(std::string format) override;
    void finish_open_notification(std::string title, std::string message, NotificationLevel level, float progress);

private:
    struct ChildState
    {
        std::unique_ptr<TrackedSourceFile> tracked_source;
    };

    void refresh_active_children();
    void remove_inactive_children();

    std::unordered_map<std::string, ChildState> _children;
    std::vector<std::string> _active_file_order;
    std::unordered_set<std::string> _active_file_paths;
    NotificationHandle _sort_progress_notification;
    NotificationHandle _open_progress_notification;
    bool _report_open_progress = true;
};

} // namespace slayerlog
