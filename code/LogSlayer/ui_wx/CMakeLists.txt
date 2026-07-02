# logslayer_ui_wx: the WxWidgets presentation layer (docs/wx-ui-plan.md).
# Depends on logslayer_core only -- never on logslayer_ui (FTXUI).
file(GLOB_RECURSE LOGSLAYER_UI_WX_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_library(logslayer_ui_wx STATIC ${LOGSLAYER_UI_WX_SOURCES})

target_include_directories(logslayer_ui_wx PUBLIC .)

target_link_libraries(logslayer_ui_wx PUBLIC logslayer_core wx::core wx::base)
