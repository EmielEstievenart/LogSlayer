# logslayer_ui: the FTXUI terminal UI for LogSlayer.
#
# The LogView rendering stack, command palette controller/view + widgets, the
# interactive (picker) commands, the command registrar, the toast sink, the
# colour theme, and the LogViewBridge that implements the core LogViewService on
# top of FTXUI. Depends on logslayer_core plus FTXUI; a different UI toolkit would
# replace this whole target while reusing logslayer_core unchanged.

file(
  GLOB_RECURSE
  LOGSLAYER_UI_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

add_library(logslayer_ui STATIC ${LOGSLAYER_UI_SOURCES})

target_include_directories(
  logslayer_ui
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
         ${CMAKE_CURRENT_SOURCE_DIR}/LogView
         ${CMAKE_CURRENT_SOURCE_DIR}/commands
         ${CMAKE_CURRENT_SOURCE_DIR}/commands/command_widgets
         ${CMAKE_CURRENT_SOURCE_DIR}/notifications)

target_link_libraries(logslayer_ui PUBLIC logslayer_core component
                                          ftxui_components)
