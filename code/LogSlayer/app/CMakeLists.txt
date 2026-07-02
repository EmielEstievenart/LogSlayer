# LogSlayer: the application that binds the core and UIs together.
#
# Just the composition roots: main.cpp parses the command line and dispatches
# to run_tui (FTXUI) or run_gui (WxWidgets), each of which wires the shared
# core model + watcher thread to its toolkit's views. All real functionality
# lives in logslayer_core (logic) and logslayer_ui / logslayer_ui_wx
# (presentation).

add_executable(LogSlayer main.cpp run_tui.cpp run_gui.cpp log4cplus.ini)

target_link_libraries(LogSlayer PRIVATE logslayer_ui logslayer_ui_wx)

add_custom_command(
  TARGET LogSlayer
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/log4cplus.ini"
    "$<TARGET_FILE_DIR:LogSlayer>/log4cplus.ini"
  COMMENT "Copy log4cplus.ini next to LogSlayer executable")
