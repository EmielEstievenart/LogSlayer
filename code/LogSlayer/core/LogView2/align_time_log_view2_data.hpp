#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include "LogView2/log_view2_data.hpp"

namespace slayerlog
{

class AlignTimeSession;

/// LogView2Data adapter over an AlignTimeSession that renders one side of the dual
/// alignment view. Both panes share the session's single row ordering (so row N lines
/// up across panes); each pane blanks the rows it does not own:
///   Left  - shows the backdrop (all other sources); blanks the aligning source's rows.
///   Right - shows the aligning source's rows; blanks the backdrop.
/// Blanked rows render as empty strings, preserving row height so the two panes overlay.
class AlignTimeLogView2Data : public LogView2Data
{
public:
    enum class Side
    {
        Left,
        Right,
    };

    AlignTimeLogView2Data(const AlignTimeSession& session, std::mutex& mutex, Side side);

    [[nodiscard]] Lock lock() const override;
    [[nodiscard]] std::size_t size() const override;
    [[nodiscard]] int widest_line_width() const override;
    [[nodiscard]] std::string to_string(std::size_t index) const override;
    CallbackId add_update_callback(UpdateCallback callback) override;
    void remove_update_callback(CallbackId callback_id) override;

private:
    const AlignTimeSession& _session;
    std::mutex& _mutex;
    Side _side;

    std::vector<std::pair<CallbackId, UpdateCallback>> _callbacks;
    CallbackId _next_callback_id = 1;
};

} // namespace slayerlog
