#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "log_types.hpp"

namespace slayerlog
{

class AllProcessedSources;

class LogViewData
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

    virtual ~LogViewData() = default;

    [[nodiscard]] virtual Lock lock() const                              = 0;
    [[nodiscard]] virtual std::size_t size() const                       = 0;
    [[nodiscard]] virtual int widest_line_width() const                  = 0;
    [[nodiscard]] virtual std::string to_string(std::size_t index) const = 0;
    virtual CallbackId add_update_callback(UpdateCallback callback)      = 0;
    virtual void remove_update_callback(CallbackId callback_id)          = 0;

    // Read-only view-model status, surfaced by the view's status bar. These are
    // non-pure with "nothing set" defaults so adapters that expose only text
    // (the align-time blanking adapter, test fakes) need not implement them.
    [[nodiscard]] virtual std::vector<std::string> include_filters() const { return {}; }
    [[nodiscard]] virtual std::vector<std::string> exclude_filters() const { return {}; }
    [[nodiscard]] virtual std::optional<int> hidden_before_line() const { return std::nullopt; }
    [[nodiscard]] virtual std::optional<HiddenColumnRange> hidden_columns() const { return std::nullopt; }
    [[nodiscard]] virtual bool updates_paused() const { return false; }
};

class AllProcessedSourcesLogViewData : public LogViewData
{
public:
    AllProcessedSourcesLogViewData(const AllProcessedSources& processed_sources, std::mutex& mutex);

    [[nodiscard]] Lock lock() const override;
    [[nodiscard]] std::size_t size() const override;
    [[nodiscard]] int widest_line_width() const override;
    [[nodiscard]] std::string to_string(std::size_t index) const override;
    CallbackId add_update_callback(UpdateCallback callback) override;
    void remove_update_callback(CallbackId callback_id) override;

    [[nodiscard]] std::vector<std::string> include_filters() const override;
    [[nodiscard]] std::vector<std::string> exclude_filters() const override;
    [[nodiscard]] std::optional<int> hidden_before_line() const override;
    [[nodiscard]] std::optional<HiddenColumnRange> hidden_columns() const override;
    [[nodiscard]] bool updates_paused() const override;

private:
    const AllProcessedSources& _processed_sources;
    std::mutex& _mutex;
};

} // namespace slayerlog
