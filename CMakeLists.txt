cmake_minimum_required(VERSION 3.14)
project(SimpleFTXUITUI)

set(CMAKE_CXX_STANDARD 17)

# FetchContent to include FTXUI
include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG main
)
FetchContent_MakeAvailable(ftxui)

add_executable(SimpleTUI main.cpp)
target_link_libraries(SimpleTUI PRIVATE ftxui::screen ftxui::dom ftxui::component)

