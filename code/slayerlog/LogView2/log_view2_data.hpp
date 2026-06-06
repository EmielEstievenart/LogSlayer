#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>

#include "log_types.hpp"

namespace slayerlog
{

class AllTrackedSources;

class LogView2Data
{
public:
    using UpdateCallback = std::function<void(VisibleLineIndex)>;
    using CallbackId     = std::size_t;

    class Lock
    {
    public:
        explicit Lock(std::mutex& mutex);

    private:
        std::unique_lock<std::mutex> _lock;
    };

    virtual ~LogView2Data() = default;

    [[nodiscard]] virtual Lock lock() const                              = 0;
    [[nodiscard]] virtual std::size_t size() const                       = 0;
    [[nodiscard]] virtual int widest_line_width() const                  = 0;
    [[nodiscard]] virtual std::string to_string(std::size_t index) const = 0;
    virtual CallbackId add_update_callback(UpdateCallback callback)      = 0;
    virtual void remove_update_callback(CallbackId callback_id)          = 0;
};

class AllTrackedSourcesLogView2Data : public LogView2Data
{
public:
    AllTrackedSourcesLogView2Data(const AllTrackedSources& tracked_sources, std::mutex& mutex);

    [[nodiscard]] Lock lock() const override;
    [[nodiscard]] std::size_t size() const override;
    [[nodiscard]] int widest_line_width() const override;
    [[nodiscard]] std::string to_string(std::size_t index) const override;
    CallbackId add_update_callback(UpdateCallback callback) override;
    void remove_update_callback(CallbackId callback_id) override;

private:
    const AllTrackedSources& _tracked_sources;
    std::mutex& _mutex;
    // Widest formatted time and source number seen so far, so every line pads to the same columns.
    mutable int _widest_time_width   = 0;
    mutable int _widest_source_width = 0;
};

} // namespace slayerlog
