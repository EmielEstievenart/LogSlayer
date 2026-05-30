#pragma once

#include <cstddef>
#include <mutex>
#include <string>

namespace slayerlog
{

class AllTrackedSources;

class LogView2Data
{
public:
    class Lock
    {
    public:
        explicit Lock(std::mutex& mutex);

    private:
        std::unique_lock<std::mutex> _lock;
    };

    virtual ~LogView2Data() = default;

    [[nodiscard]] virtual Lock lock() const = 0;
    [[nodiscard]] virtual std::size_t size() const = 0;
    [[nodiscard]] virtual int widest_line_width() const = 0;
    [[nodiscard]] virtual std::string to_string(std::size_t index) const = 0;
};

class AllTrackedSourcesLogView2Data : public LogView2Data
{
public:
    AllTrackedSourcesLogView2Data(const AllTrackedSources& tracked_sources, std::mutex& mutex);

    [[nodiscard]] Lock lock() const override;
    [[nodiscard]] std::size_t size() const override;
    [[nodiscard]] int widest_line_width() const override;
    [[nodiscard]] std::string to_string(std::size_t index) const override;

private:
    const AllTrackedSources& _tracked_sources;
    std::mutex& _mutex;
};

} // namespace slayerlog
