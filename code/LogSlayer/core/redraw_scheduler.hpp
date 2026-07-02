#pragma once

namespace slayerlog
{

/// Thread-safe hook a UI provides so core code can request a repaint after
/// mutating the model. Implementations must tolerate calls from any thread
/// (the watcher thread posts through this) and should coalesce bursts; a
/// request is a hint that the pull-based view should re-render, not a
/// synchronous paint.
class RedrawScheduler
{
public:
    virtual ~RedrawScheduler() = default;

    virtual void request_redraw() = 0;
};

} // namespace slayerlog
