#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "log_source.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/source_resolver.hpp"
#include "tracked_sources/tracked_source_factory.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_temp_dir()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto dir           = std::filesystem::temp_directory_path() / ("slayerlog_resolver_" + unique_suffix);
    std::filesystem::create_directories(dir);
    return dir;
}

void write_log(const std::filesystem::path& path)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "plain line\n";
}

} // namespace

TEST(SourceResolverTest, ResolvesByPathMnemonicAndEquivalentSpelling)
{
    const auto dir   = make_temp_dir();
    const auto log_a = dir / "alpha.log";
    const auto log_b = dir / "beta.log";
    write_log(log_a);
    write_log(log_b);

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_a.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_b.string())).has_value());

    EXPECT_EQ(resolve_source_index(tracked_sources, log_a.string()), std::optional<std::size_t>(0));
    EXPECT_EQ(resolve_source_index(tracked_sources, log_b.string()), std::optional<std::size_t>(1));

    // A differently spelled path to the same file resolves through the
    // normalized source identity.
    const auto redundant_spelling = (dir / "." / "beta.log").string();
    EXPECT_EQ(resolve_source_index(tracked_sources, redundant_spelling), std::optional<std::size_t>(1));

    // Mnemonics resolve case-insensitively.
    const auto mnemonics = tracked_sources.source_mnemonics();
    ASSERT_EQ(mnemonics.size(), 2U);
    ASSERT_FALSE(mnemonics[1].empty());
    std::string uppercase_mnemonic = mnemonics[1];
    std::transform(uppercase_mnemonic.begin(), uppercase_mnemonic.end(), uppercase_mnemonic.begin(), [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    EXPECT_EQ(resolve_source_index(tracked_sources, uppercase_mnemonic), std::optional<std::size_t>(1));

    std::error_code error_code;
    std::filesystem::remove_all(dir, error_code);
}

TEST(SourceResolverTest, ResolvesFolderSourcesByFolderPath)
{
    const auto dir = make_temp_dir();
    write_log(dir / "inside.log");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, make_local_folder_source(dir.string())).has_value());

    EXPECT_EQ(resolve_source_index(tracked_sources, dir.string()), std::optional<std::size_t>(0));

    std::error_code error_code;
    std::filesystem::remove_all(dir, error_code);
}

TEST(SourceResolverTest, ReportsUnknownReferences)
{
    const auto dir   = make_temp_dir();
    const auto log_a = dir / "alpha.log";
    write_log(log_a);

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_a.string())).has_value());

    EXPECT_FALSE(resolve_source_index(tracked_sources, "").has_value());
    EXPECT_FALSE(resolve_source_index(tracked_sources, "no-such-source.log").has_value());
    EXPECT_FALSE(resolve_source_index(tracked_sources, (dir / "other.log").string()).has_value());

    std::error_code error_code;
    std::filesystem::remove_all(dir, error_code);
}

} // namespace slayerlog
