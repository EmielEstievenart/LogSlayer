add_executable(
  unit_tests
  slayerlog/file_watcher_tests.cpp
  slayerlog/blf_file_watcher_tests.cpp
  slayerlog/zstd_file_watcher_tests.cpp
  slayerlog/log_source_tests.cpp
  slayerlog/tracked_source_tests.cpp
  slayerlog/tracked_source_manager_tests.cpp
  slayerlog/stream_line_buffer_tests.cpp
  slayerlog/command_palette_controller_tests.cpp
  slayerlog/command_registrar_tests.cpp
  slayerlog/command_line_parser_tests.cpp
  slayerlog/command_history_tests.cpp
  slayerlog/command_manager_tests.cpp
  slayerlog/command_widget_tests.cpp
  slayerlog/log_batch_tests.cpp
  slayerlog/log_timestamp_tests.cpp
  slayerlog/log_model_tests.cpp
  slayerlog/log_controller_tests.cpp
  slayerlog/master_controller_tests.cpp
  slayerlog/toast_component_tests.cpp
  slayerlog/settings_ini_tests.cpp
  ${SLAYERLOG_CORE_SOURCES})

target_link_libraries(
  unit_tests
  PRIVATE eestv_lib
          ftxui_components
          component
          GTest::gtest_main
          Boost::log
          Boost::log_setup
          Boost::filesystem
          Boost::thread
          Boost::program_options
          log4cplus::log4cplus
          libzstd
          nlohmann_json::nlohmann_json
          SimpleIni::SimpleIni)

target_compile_definitions(unit_tests PRIVATE BOOST_ALL_NO_LIB BOOST_USE_WINAPI_VERSION=0x0602)
target_include_directories(
  unit_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/code/slayerlog
          ${CMAKE_SOURCE_DIR}/code/slayerlog/commands
          ${CMAKE_SOURCE_DIR}/code/slayerlog/notifications
          ${CMAKE_SOURCE_DIR}/code/slayerlog/tracked_sources
          ${CMAKE_SOURCE_DIR}/libs/eestv)

if(ENABLE_CTEST)
  include(GoogleTest)
  gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST)
endif()
