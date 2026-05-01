#include "stream_line_buffer.hpp"

namespace slayerlog
{

std::size_t StreamLineBuffer::append(std::string_view chunk, std::vector<std::string>& lines)
{
    std::size_t committed_bytes = 0;
    std::size_t start           = 0;

    while (start < chunk.size())
    {
        const auto newline = chunk.find('\n', start);
        if (newline == std::string_view::npos)
        {
            _pending_fragment.append(chunk.substr(start));
            break;
        }

        _pending_fragment.append(chunk.substr(start, newline - start));
        std::string line = _pending_fragment;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        lines.push_back(std::move(line));
        committed_bytes += _pending_fragment.size() + 1;
        _pending_fragment.clear();
        start = newline + 1;
    }

    return committed_bytes;
}

void StreamLineBuffer::discard_pending_fragment()
{
    _pending_fragment.clear();
}

bool StreamLineBuffer::has_pending_fragment() const noexcept
{
    return !_pending_fragment.empty();
}

} // namespace slayerlog
