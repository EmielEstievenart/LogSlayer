#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "session_script.hpp"

namespace slayerlog
{

TEST(SessionScriptTest, DetectsScriptKindFromExtension)
{
    EXPECT_EQ(session_script_kind_for_path("session.bat"), SessionScriptKind::WindowsBatch);
    EXPECT_EQ(session_script_kind_for_path("SESSION.CMD"), SessionScriptKind::WindowsBatch);
    EXPECT_EQ(session_script_kind_for_path("session.sh"), SessionScriptKind::PosixShell);
    EXPECT_FALSE(session_script_kind_for_path("session.txt").has_value());
    EXPECT_FALSE(session_script_kind_for_path("session").has_value());
}

TEST(SessionScriptTest, BatchScriptQuotesArgumentsAndEscapesPercents)
{
    const std::string script = build_session_script(SessionScriptKind::WindowsBatch, "C:\\tools\\LogSlayer.exe", {"open C:\\logs\\a.log", "set-time-format a.log hh:mm 100%"});

    EXPECT_NE(script.find("@echo off\r\n"), std::string::npos);
    EXPECT_NE(script.find("\"C:\\tools\\LogSlayer.exe\" ^\r\n"), std::string::npos);
    EXPECT_NE(script.find("    --cmd \"open C:\\logs\\a.log\" ^\r\n"), std::string::npos);
    EXPECT_NE(script.find("    --cmd \"set-time-format a.log hh:mm 100%%\"\r\n"), std::string::npos);
}

TEST(SessionScriptTest, BatchQuotingFollowsCrtBackslashRules)
{
    // A trailing backslash before the closing quote must be doubled, and an
    // embedded quote becomes \" so the CRT parses the argument back verbatim.
    const std::string script = build_session_script(SessionScriptKind::WindowsBatch, "LogSlayer", {"open C:\\logs\\", "filter-in say \"hi\""});

    EXPECT_NE(script.find("--cmd \"open C:\\logs\\\\\""), std::string::npos);
    EXPECT_NE(script.find("--cmd \"filter-in say \\\"hi\\\"\""), std::string::npos);
}

TEST(SessionScriptTest, ShellScriptUsesSingleQuotesAndContinuations)
{
    const std::string script = build_session_script(SessionScriptKind::PosixShell, "/usr/local/bin/LogSlayer", {"open /var/log/a.log", "filter-in can't"});

    EXPECT_NE(script.find("#!/bin/sh\n"), std::string::npos);
    EXPECT_NE(script.find("'/usr/local/bin/LogSlayer' \\\n"), std::string::npos);
    EXPECT_NE(script.find("    --cmd 'open /var/log/a.log' \\\n"), std::string::npos);
    EXPECT_NE(script.find("    --cmd 'filter-in can'\\''t'\n"), std::string::npos);
    EXPECT_EQ(script.find('\r'), std::string::npos);
}

TEST(SessionScriptTest, InvocationLineJoinsEverythingOnOneLine)
{
    const std::string invocation = build_session_invocation_line(SessionScriptKind::WindowsBatch, "C:\\tools\\LogSlayer.exe", {"open a.log", "go-to-line 42"});

    EXPECT_EQ(invocation, "\"C:\\tools\\LogSlayer.exe\" --cmd \"open a.log\" --cmd \"go-to-line 42\"");
}

TEST(SessionScriptTest, ScriptWithoutCommandsIsJustTheExecutable)
{
    const std::string script = build_session_script(SessionScriptKind::PosixShell, "/bin/LogSlayer", {});

    EXPECT_EQ(script, "#!/bin/sh\n'/bin/LogSlayer'\n");
}

} // namespace slayerlog
