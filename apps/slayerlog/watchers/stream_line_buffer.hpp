#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace slayerlog
{

class StreamLineBuffer
{
public:
    std::size_t append(std::string_view chunk, std::vector<std::string>& lines);
    void discard_pending_fragment();
    bool has_pending_fragment() const noexcept;

private:
    std::string _pending_fragment;
};

} // namespace slayerlog
