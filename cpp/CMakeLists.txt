cmake_minimum_required(VERSION 3.14)
project(MaiMail)

set(CMAKE_CXX_STANDARD 17)

# FetchContent to include FTXUI
include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG main
)
FetchContent_MakeAvailable(ftxui)

add_executable(MaiMail main.cpp)
target_link_libraries(MaiMail PRIVATE ftxui::screen ftxui::dom ftxui::component)

