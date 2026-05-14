#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "settings_store.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_temp_settings_directory()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_settings_store_tests_" + unique_suffix);
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void write_file(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

} // namespace

TEST(SettingsStoreTest, SaveCreatesBackupBeforeReplacingExistingSettingsFile)
{
    const auto settings_directory = make_temp_settings_directory();
    const auto settings_path      = settings_directory / "settings.ini";
    const std::string original    = "[command_history]\nentry=find old\n";
    write_file(settings_path, original);

    SettingsStore settings_store(settings_path);
    std::string error_message;
    ASSERT_TRUE(settings_store.load(error_message)) << error_message;
    settings_store.ini().set_values("command_history", "entry", {"find new"});

    ASSERT_TRUE(settings_store.save(error_message)) << error_message;

    EXPECT_NE(read_file(settings_path).find("entry=find new"), std::string::npos);

    std::vector<std::filesystem::path> backups;
    for (const auto& entry : std::filesystem::directory_iterator(settings_directory))
    {
        if (entry.path().extension() == ".bak")
        {
            backups.push_back(entry.path());
        }
    }

    ASSERT_EQ(backups.size(), 1U);
    EXPECT_EQ(read_file(backups.front()), original);

    std::error_code error_code;
    std::filesystem::remove_all(settings_directory, error_code);
}

} // namespace slayerlog
