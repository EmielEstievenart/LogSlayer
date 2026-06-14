# logslayer_core: the UI-framework-free heart of LogSlayer.
#
# Watchers, tracked/processed sources, sorting/merging, filtering, searching,
# timestamps, settings, the command model + pure command actions, and the
# LogViewService abstraction the UI implements. This target deliberately links
# NO FTXUI (neither `component` nor `ftxui_components`): if any file here pulls
# in an <ftxui/...> header it fails to compile, which is what keeps the core
# portable to a different UI toolkit (e.g. Qt).

file(
  GLOB_RECURSE
  LOGSLAYER_CORE_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

add_library(logslayer_core STATIC ${LOGSLAYER_CORE_SOURCES})

target_include_directories(
  logslayer_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
         ${CMAKE_CURRENT_SOURCE_DIR}/commands
         ${CMAKE_CURRENT_SOURCE_DIR}/tracked_sources
         ${CMAKE_CURRENT_SOURCE_DIR}/timestamp
         ${CMAKE_CURRENT_SOURCE_DIR}/watchers
         ${CMAKE_CURRENT_SOURCE_DIR}/LogView2
         ${CMAKE_CURRENT_SOURCE_DIR}/notifications
         ${CMAKE_SOURCE_DIR}/libs/eestv)

target_compile_definitions(logslayer_core PUBLIC BOOST_ALL_NO_LIB
                                                 BOOST_USE_WINAPI_VERSION=0x0602)

target_link_libraries(
  logslayer_core
  PUBLIC Boost::program_options
         Boost::filesystem
         Boost::thread
         log4cplus::log4cplus
         libzstd
         eestv_lib
         nlohmann_json::nlohmann_json
         SimpleIni::SimpleIni)

if(WIN32)
  target_link_libraries(logslayer_core PUBLIC user32)
endif()
