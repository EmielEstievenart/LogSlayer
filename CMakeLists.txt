cmake_minimum_required(VERSION 3.21)

project(LogSlayer)

set(BOOST_ROOT
    "$ENV{BOOST_ROOT}"
    CACHE PATH "Path to the Boost source tree used by add_subdirectory()")

if(NOT BOOST_ROOT)
  message(
    FATAL_ERROR
      "BOOST_ROOT is not set. Pass -DBOOST_ROOT=<path-to-boost> or set the BOOST_ROOT environment variable."
  )
endif()

set(BOOST_INCLUDE_LIBRARIES
    asio
    system
    program_options
    log
    filesystem
    thread
    regex
    date_time
    atomic)
set(BOOST_ENABLE_CMAKE ON)
add_subdirectory("${BOOST_ROOT}" my_boost)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0601)
endif()

option(ENABLE_CTEST "Enable CTest and test discovery" ON)
if(ENABLE_CTEST)
  enable_testing()
endif()

set(FTXUI_BUILD_DOCS
    OFF
    CACHE BOOL "Disable FTXUI docs in the superproject" FORCE)
set(FTXUI_BUILD_EXAMPLES
    OFF
    CACHE BOOL "Disable FTXUI examples in the superproject" FORCE)
set(FTXUI_BUILD_TESTS
    OFF
    CACHE BOOL "Disable FTXUI tests in the superproject" FORCE)
set(FTXUI_BUILD_TESTS_FUZZER
    OFF
    CACHE BOOL "Disable FTXUI fuzz tests in the superproject" FORCE)
set(FTXUI_BUILD_MODULES
    OFF
    CACHE BOOL "Disable FTXUI C++20 modules in the superproject" FORCE)
add_subdirectory(libs/ftxui)
add_subdirectory(libs/ftxui_components/code)

set(gtest_force_shared_crt
    ON
    CACHE BOOL "Use the shared CRT for GoogleTest on MSVC" FORCE)
if(WIN32)
  set(gtest_disable_pthreads
      ON
      CACHE BOOL "Disable pthreads for GoogleTest on Windows" FORCE)
endif()
add_subdirectory(libs/googletest)

set(LOG4CPLUS_BUILD_TESTING
    OFF
    CACHE BOOL "Disable log4cplus tests in the superproject" FORCE)
set(LOG4CPLUS_BUILD_LOGGINGSERVER
    OFF
    CACHE BOOL "Disable log4cplus logging server in the superproject" FORCE)
set(WITH_UNIT_TESTS
    OFF
    CACHE BOOL "Disable log4cplus unit tests in the superproject" FORCE)
set(_WIN32_WINNT
    0x0601
    CACHE STRING "Windows API version for log4cplus" FORCE)
if(NOT DEFINED BUILD_SHARED_LIBS)
  set(BUILD_SHARED_LIBS
      OFF
      CACHE BOOL "Build static libraries by default")
endif()
add_subdirectory(libs/log4cplus)

set(ZSTD_BUILD_PROGRAMS
    OFF
    CACHE BOOL "Disable zstd command-line programs in the superproject" FORCE)
set(ZSTD_BUILD_TESTS
    OFF
    CACHE BOOL "Disable zstd tests in the superproject" FORCE)
set(ZSTD_BUILD_CONTRIB
    OFF
    CACHE BOOL "Disable zstd contrib targets in the superproject" FORCE)
set(ZSTD_BUILD_SHARED
    OFF
    CACHE BOOL "Disable shared zstd library in the superproject" FORCE)
set(ZSTD_BUILD_STATIC
    ON
    CACHE BOOL "Build static zstd library in the superproject" FORCE)
add_subdirectory(libs/zstd)

add_subdirectory(libs/nlohmann_json)
add_subdirectory(libs/simpleini)
add_subdirectory(libs/eestv)

# The wx GUI tier is optional: on Linux, building wxWidgets requires GTK3/X11
# dev packages at configure time, so it defaults off there (and sanitizer runs
# never need it). Without it, --ui gui reports that the build has no GUI.
if(WIN32 OR APPLE)
  set(LOGSLAYER_ENABLE_WX_UI_DEFAULT ON)
else()
  set(LOGSLAYER_ENABLE_WX_UI_DEFAULT OFF)
endif()
option(LOGSLAYER_ENABLE_WX_UI "Build the WxWidgets GUI tier"
       ${LOGSLAYER_ENABLE_WX_UI_DEFAULT})

if(LOGSLAYER_ENABLE_WX_UI)
  set(wxBUILD_SHARED
      OFF
      CACHE BOOL "Build wxWidgets statically in the superproject" FORCE)
  set(wxBUILD_SAMPLES
      OFF
      CACHE STRING "Disable wxWidgets samples in the superproject" FORCE)
  set(wxBUILD_TESTS
      OFF
      CACHE STRING "Disable wxWidgets tests in the superproject" FORCE)
  set(wxBUILD_DEMOS
      OFF
      CACHE BOOL "Disable wxWidgets demos in the superproject" FORCE)
  set(wxBUILD_BENCHMARKS
      OFF
      CACHE BOOL "Disable wxWidgets benchmarks in the superproject" FORCE)
  set(wxBUILD_INSTALL
      OFF
      CACHE BOOL "Disable wxWidgets install rules in the superproject" FORCE)
  set(wxUSE_WEBVIEW
      OFF
      CACHE BOOL "Disable wxWebView (avoids the WebView2 dependency)" FORCE)
  add_subdirectory(libs/wxwidgets)
endif()

# LogSlayer is split into four projects so the core stays portable across UIs:
#   core -> logslayer_core (no UI toolkit)   ui -> logslayer_ui (FTXUI)
#   ui_wx -> logslayer_ui_wx (WxWidgets)     app -> LogSlayer
add_subdirectory(code/LogSlayer/core)
add_subdirectory(code/LogSlayer/ui)
if(LOGSLAYER_ENABLE_WX_UI)
  add_subdirectory(code/LogSlayer/ui_wx)
endif()
add_subdirectory(code/LogSlayer/app)
add_subdirectory(tests/unit_tests)
