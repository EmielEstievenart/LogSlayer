#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "watchers/stream_line_buffer.hpp"

namespace slayerlog
{

TEST(StreamLineBufferTest, EmitsCompleteLinesAndTracksCommittedBytes)
{
    StreamLineBuffer buffer;
    std::vector<std::string> lines;

    const std::size_t committed_bytes = buffer.append("first\nsecond\n", lines);

    EXPECT_EQ(committed_bytes, std::string("first\nsecond\n").size());
    EXPECT_EQ(lines, (std::vector<std::string> {"first", "second"}));
    EXPECT_FALSE(buffer.has_pending_fragment());
}

TEST(StreamLineBufferTest, HoldsIncompleteTrailingFragment)
{
    StreamLineBuffer buffer;
    std::vector<std::string> lines;

    EXPECT_EQ(buffer.append("partial", lines), 0U);
    EXPECT_TRUE(lines.empty());
    EXPECT_TRUE(buffer.has_pending_fragment());

    EXPECT_EQ(buffer.append(" line\nnext\n", lines), std::string("partial line\nnext\n").size());
    EXPECT_EQ(lines, (std::vector<std::string> {"partial line", "next"}));
    EXPECT_FALSE(buffer.has_pending_fragment());
}

TEST(StreamLineBufferTest, DiscardsPendingFragment)
{
    StreamLineBuffer buffer;
    std::vector<std::string> lines;

    buffer.append("partial", lines);
    buffer.discard_pending_fragment();

    EXPECT_FALSE(buffer.has_pending_fragment());
    EXPECT_EQ(buffer.append("fresh\n", lines), std::string("fresh\n").size());
    EXPECT_EQ(lines, (std::vector<std::string> {"fresh"}));
}

} // namespace slayerlog
