#include "log_watcher_factory.hpp"

#include "file_watcher.hpp"
#include "ssh_tail_watcher.hpp"

namespace slayerlog
{

std::unique_ptr<LogWatcherBase> create_log_watcher_for_source(const LogSource& source)
{
    if (source.kind == LogSourceKind::SshRemoteFile)
    {
        return std::make_unique<SshTailWatcher>(source);
    }

    if (source.kind == LogSourceKind::LocalFile)
    {
        return std::make_unique<FileWatcher>(source.local_path);
    }

    return nullptr;
}

} // namespace slayerlog
