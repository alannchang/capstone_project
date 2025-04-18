cmake_minimum_required(VERSION 3.10)
project(MaiMail)

set(CMAKE_CXX_STANDARD 17)
set(LLAMA_BUILD_COMMON On)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)

include(FetchContent)

# pybind11
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.13.6
)
FetchContent_MakeAvailable(pybind11)

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
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(json)

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
        pybind11::embed
)

target_include_directories(
        chat
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
)
