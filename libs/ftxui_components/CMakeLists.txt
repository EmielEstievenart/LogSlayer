add_library(
  ftxui_components
  text_view_controller.cpp
  ftxui_components/text_view_controller.hpp
  text_view_view.cpp
  ftxui_components/text_view_view.hpp)

target_include_directories(ftxui_components PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ftxui_components PUBLIC component)

if(WIN32)
  target_link_libraries(ftxui_components PRIVATE user32)
endif()
