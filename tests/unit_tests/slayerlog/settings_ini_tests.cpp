#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "settings_ini.hpp"

namespace slayerlog
{

TEST(SettingsIniTest, ParsesRepeatedValuesFromSection)
{
    SettingsIni ini;
    std::string error_message;

    const std::string ini_text = "[command_history]\n"
                                 "entry=find error\n"
                                 "entry=filter-in auth\n";

    ASSERT_TRUE(ini.parse(ini_text, error_message));

    const std::vector<std::string> values = ini.values("command_history", "entry");
    ASSERT_EQ(values.size(), 2U);
    EXPECT_EQ(values[0], "find error");
    EXPECT_EQ(values[1], "filter-in auth");
}

TEST(SettingsIniTest, SetValuesReplacesExistingEntriesForKey)
{
    SettingsIni ini;
    std::string error_message;

    ASSERT_TRUE(ini.parse("[command_history]\nentry=old\n", error_message));
    ini.set_values("command_history", "entry", {"new-one", "new-two"});

    const auto serialized = ini.serialize();
    EXPECT_NE(serialized.find("entry=new-one"), std::string::npos);
    EXPECT_NE(serialized.find("entry=new-two"), std::string::npos);
    EXPECT_EQ(serialized.find("entry=old"), std::string::npos);
}

TEST(SettingsIniTest, ReturnsErrorForMalformedSectionLine)
{
    SettingsIni ini;
    std::string error_message;

    EXPECT_FALSE(ini.parse("[command_history\nentry=find error\n", error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(SettingsIniTest, StoresTimestampFormatsAsRepeatedValues)
{
    SettingsIni ini;
    ini.set_values("timestamp_formats", "format", {"YYYY-MM-DDThh:mm:ss", "DD-MMM-YYYY hh:mm:ss"});

    const auto serialized = ini.serialize();
    EXPECT_NE(serialized.find("[timestamp_formats]"), std::string::npos);
    EXPECT_NE(serialized.find("format=YYYY-MM-DDThh:mm:ss"), std::string::npos);
    EXPECT_NE(serialized.find("format=DD-MMM-YYYY hh:mm:ss"), std::string::npos);
}

} // namespace slayerlog
