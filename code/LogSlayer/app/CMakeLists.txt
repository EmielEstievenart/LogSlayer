# LogSlayer: the application that binds the core and UI together.
#
# Just the composition root (main.cpp): it wires the core model + watcher thread
# to the FTXUI views and command palettes. All real functionality lives in
# logslayer_core (logic) and logslayer_ui (presentation).

add_executable(LogSlayer main.cpp log4cplus.ini)

target_link_libraries(LogSlayer PRIVATE logslayer_ui)

add_custom_command(
  TARGET LogSlayer
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/log4cplus.ini"
    "$<TARGET_FILE_DIR:LogSlayer>/log4cplus.ini"
  COMMENT "Copy log4cplus.ini next to LogSlayer executable")
