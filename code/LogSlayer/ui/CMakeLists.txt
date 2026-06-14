# logslayer_ui: the FTXUI terminal UI for LogSlayer.
#
# The LogView2 rendering stack, command palette controller/view + widgets, the
# interactive (picker) commands, the command registrar, the toast sink, the
# colour theme, and the LogView2Bridge that implements the core LogViewService on
# top of FTXUI. Depends on logslayer_core plus FTXUI; a different UI toolkit would
# replace this whole target while reusing logslayer_core unchanged.
#
# The legacy LogView/ stack (LogView1: LogController, LogViewComponent,
# LogViewBridge) is kept on disk for reference but excluded from the build below
# now that LogView2 is the sole view.

file(
  GLOB_RECURSE
  LOGSLAYER_UI_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

list(FILTER LOGSLAYER_UI_SOURCES EXCLUDE REGEX "/LogView/")

add_library(logslayer_ui STATIC ${LOGSLAYER_UI_SOURCES})

target_include_directories(
  logslayer_ui
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
         ${CMAKE_CURRENT_SOURCE_DIR}/LogView2
         ${CMAKE_CURRENT_SOURCE_DIR}/commands
         ${CMAKE_CURRENT_SOURCE_DIR}/commands/command_widgets
         ${CMAKE_CURRENT_SOURCE_DIR}/notifications)

target_link_libraries(logslayer_ui PUBLIC logslayer_core component
                                          ftxui_components)
