#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace slayerlog
{

struct VisibleLineIndex
{
    int value = 0;
};

inline bool operator==(VisibleLineIndex lhs, VisibleLineIndex rhs)
{
    return lhs.value == rhs.value;
}

inline bool operator<(VisibleLineIndex lhs, VisibleLineIndex rhs)
{
    return lhs.value < rhs.value;
}

struct AllLineIndex
{
    int value = 0;
};

inline bool operator==(AllLineIndex lhs, AllLineIndex rhs)
{
    return lhs.value == rhs.value;
}

inline bool operator<(AllLineIndex lhs, AllLineIndex rhs)
{
    return lhs.value < rhs.value;
}

struct FindResultIndex
{
    int value = 0;
};

inline bool operator==(FindResultIndex lhs, FindResultIndex rhs)
{
    return lhs.value == rhs.value;
}

inline bool operator<(FindResultIndex lhs, FindResultIndex rhs)
{
    return lhs.value < rhs.value;
}

template <typename T, typename Index>
class IndexedVector
{
public:
    using iterator       = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;

    T& operator[](Index index) { return _items[static_cast<std::size_t>(index.value)]; }

    const T& operator[](Index index) const { return _items[static_cast<std::size_t>(index.value)]; }

    void clear() { _items.clear(); }

    void reserve(std::size_t count) { _items.reserve(count); }

    void push_back(const T& value) { _items.push_back(value); }

    void push_back(T&& value) { _items.push_back(std::move(value)); }

    [[nodiscard]] std::size_t size() const { return _items.size(); }

    [[nodiscard]] bool empty() const { return _items.empty(); }

    iterator begin() { return _items.begin(); }

    iterator end() { return _items.end(); }

    const_iterator begin() const { return _items.begin(); }

    const_iterator end() const { return _items.end(); }

    const_iterator cbegin() const { return _items.cbegin(); }

    const_iterator cend() const { return _items.cend(); }

private:
    std::vector<T> _items;
};

struct HiddenColumnRange
{
    int start = 0;
    int end   = 0;
};

inline bool operator==(const HiddenColumnRange& lhs, const HiddenColumnRange& rhs)
{
    return lhs.start == rhs.start && lhs.end == rhs.end;
}

std::optional<HiddenColumnRange> parse_hidden_column_range(std::string_view text);

} // namespace slayerlog
