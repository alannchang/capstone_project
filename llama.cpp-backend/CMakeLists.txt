cmake_minimum_required(VERSION 3.10)
project(llama_cpp_backend)

set(CMAKE_CXX_STANDARD 17)
set(LLAMA_BUILD_COMMON On)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp")

# FetchContent to include FTXUI
include(FetchContent)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG main
)

FetchContent_MakeAvailable(ftxui)

add_executable(
        chat
        LlamaInference.cpp
        main.cpp
)
target_link_libraries(
        chat
        PRIVATE
        common llama ggml
        ftxui::screen ftxui::dom ftxui::component
)
