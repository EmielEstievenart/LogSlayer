add_executable(
  unit_tests
  slayerlog/file_watcher_tests.cpp
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
  slayerlog/align_time_session_tests.cpp
  slayerlog/log_model_tests.cpp
  slayerlog/log_view_data_tests.cpp
  slayerlog/toast_component_tests.cpp
  slayerlog/settings_ini_tests.cpp
  slayerlog/settings_store_tests.cpp)

# The tests link the two LogSlayer libraries (core + ui) rather than recompiling
# the sources. All include directories, compile definitions and third-party
# dependencies are inherited transitively from the PUBLIC interfaces of the libs.
target_link_libraries(unit_tests PRIVATE logslayer_core logslayer_ui
                                         GTest::gtest_main)

if(ENABLE_CTEST)
  include(GoogleTest)
  gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST)
endif()
