cmake_minimum_required(VERSION 3.10)
project(MaiMail)

set(CMAKE_CXX_STANDARD 17)
set(LLAMA_BUILD_COMMON On)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)

include(FetchContent)

# FTXUI
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG main
)
FetchContent_MakeAvailable(ftxui)

# json
FetchContent_Declare(
    json 
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# httplib
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)

FetchContent_MakeAvailable(httplib)
file(GLOB SRC_FILES "src/*.cpp")

add_executable(
        chat
        ${SRC_FILES}
)

target_link_libraries(
        chat
        PRIVATE
        common llama ggml
        ftxui::screen ftxui::dom ftxui::component
        nlohmann_json::nlohmann_json
        httplib::httplib
)

target_include_directories(
        chat
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
)
