#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "commands/session_config_store.hpp"
#include "settings_store.hpp"

namespace slayerlog
{

namespace
{

// A directory per test so the .bak files SettingsStore::save creates are
// removed along with the ini.
std::filesystem::path make_temp_settings_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory     = std::filesystem::temp_directory_path() / ("slayerlog_config_store_" + unique_suffix);
    std::filesystem::create_directories(directory);
    return directory / "settings.ini";
}

void remove_file(const std::filesystem::path& settings_path)
{
    std::error_code error_code;
    std::filesystem::remove_all(settings_path.parent_path(), error_code);
}

} // namespace

TEST(SessionConfigStoreTest, SavedConfigRoundTripsThroughDisk)
{
    const auto settings_path = make_temp_settings_path();
    const std::vector<std::string> commands {"open C:\\logs\\a.log", "set-offset \"C:\\logs\\a.log\" +00 00:01:00", "filter-in ERROR"};

    {
        SettingsStore settings_store(settings_path);
        std::string error_message;
        ASSERT_TRUE(settings_store.load(error_message));
        EXPECT_FALSE(save_session_config(settings_store, "crashhunt", commands).has_value());
    }

    SettingsStore reloaded_store(settings_path);
    std::string error_message;
    ASSERT_TRUE(reloaded_store.load(error_message));

    const auto loaded = load_session_config(reloaded_store, "crashhunt");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, commands);

    remove_file(settings_path);
}

TEST(SessionConfigStoreTest, SavingAnExistingNameOverwritesIt)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    std::string error_message;
    ASSERT_TRUE(settings_store.load(error_message));

    ASSERT_FALSE(save_session_config(settings_store, "session", {"open a.log", "filter-in ERROR"}).has_value());
    ASSERT_FALSE(save_session_config(settings_store, "session", {"open b.log"}).has_value());

    const auto loaded = load_session_config(settings_store, "session");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, std::vector<std::string> {"open b.log"});

    remove_file(settings_path);
}

TEST(SessionConfigStoreTest, EmptyConfigStillExistsAfterSaving)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    std::string error_message;
    ASSERT_TRUE(settings_store.load(error_message));

    ASSERT_FALSE(save_session_config(settings_store, last_session_config_name, {}).has_value());

    const auto loaded = load_session_config(settings_store, last_session_config_name);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->empty());

    remove_file(settings_path);
}

TEST(SessionConfigStoreTest, ListsAndDeletesConfigs)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    std::string error_message;
    ASSERT_TRUE(settings_store.load(error_message));

    ASSERT_FALSE(save_session_config(settings_store, "alpha", {"open a.log"}).has_value());
    ASSERT_FALSE(save_session_config(settings_store, "beta", {"open b.log"}).has_value());

    EXPECT_EQ(list_session_configs(settings_store), (std::vector<std::string> {"alpha", "beta"}));

    EXPECT_FALSE(remove_session_config(settings_store, "alpha").has_value());
    EXPECT_EQ(list_session_configs(settings_store), std::vector<std::string> {"beta"});

    const auto missing_error = remove_session_config(settings_store, "alpha");
    ASSERT_TRUE(missing_error.has_value());
    EXPECT_EQ(*missing_error, "No config named 'alpha'");

    remove_file(settings_path);
}

TEST(SessionConfigStoreTest, LoadReportsMissingConfig)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    std::string error_message;
    ASSERT_TRUE(settings_store.load(error_message));

    EXPECT_FALSE(load_session_config(settings_store, "nope").has_value());

    remove_file(settings_path);
}

TEST(SessionConfigStoreTest, ValidatesConfigNames)
{
    EXPECT_TRUE(validate_session_config_name("").has_value());
    EXPECT_TRUE(validate_session_config_name("   ").has_value());
    EXPECT_TRUE(validate_session_config_name("bad[name").has_value());
    EXPECT_TRUE(validate_session_config_name("bad=name").has_value());
    EXPECT_TRUE(validate_session_config_name("bad;name").has_value());
    EXPECT_FALSE(validate_session_config_name("crashhunt").has_value());
    EXPECT_FALSE(validate_session_config_name("my crash hunt").has_value());
    EXPECT_FALSE(validate_session_config_name(last_session_config_name).has_value());
}

TEST(SessionConfigStoreTest, ConfigSectionsCoexistWithOtherSettings)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    std::string error_message;
    ASSERT_TRUE(settings_store.load(error_message));

    settings_store.ini().set_values("command_history", "command", {"filter-in ERROR"});
    ASSERT_FALSE(save_session_config(settings_store, "alpha", {"open a.log"}).has_value());

    EXPECT_EQ(settings_store.ini().values("command_history", "command"), std::vector<std::string> {"filter-in ERROR"});
    EXPECT_EQ(list_session_configs(settings_store), std::vector<std::string> {"alpha"});

    remove_file(settings_path);
}

} // namespace slayerlog
