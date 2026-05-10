#pragma once

#include <functional>
#include <string>
#include <vector>

#include "log_watcher_base.hpp"
#include "notifications/notification.hpp"

namespace slayerlog
{

class BlfFileWatcher : public LogWatcherBase
{
public:
    struct ImportResult
    {
        bool started = true;
        int exit_code = 0;
        std::vector<std::string> stdout_lines;
        std::string stderr_text;
        std::string error_message;
    };

    using ImportRunner = std::function<ImportResult(const std::string&, const Notifier&)>;

    explicit BlfFileWatcher(std::string file_path, Notifier notifier = {});
    BlfFileWatcher(std::string file_path, Notifier notifier, ImportRunner import_runner);

    BlfFileWatcher(const BlfFileWatcher&)            = delete;
    BlfFileWatcher& operator=(const BlfFileWatcher&) = delete;

protected:
    bool poll_locked(std::vector<std::string>& lines) override;

private:
    static ImportResult run_importer_process(const std::string& file_path, const Notifier& notifier);
    static std::string build_import_error_line(const std::string& file_path, const std::string& message);

    std::string _file_path;
    Notifier _notifier;
    ImportRunner _import_runner;
    bool _consumed = false;
};

} // namespace slayerlog
