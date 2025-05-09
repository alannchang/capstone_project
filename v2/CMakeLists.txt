cmake_minimum_required(VERSION 3.14)
project(llama_inference_v2 VERSION 0.1.0
    DESCRIPTION "LLama Inference Engine - Version 2"
    LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Always build with position-independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Basic options
option(BUILD_TESTS "Build test suite" ON)
option(ENABLE_WARNINGS "Enable warnings" ON)

# Hardware acceleration options
option(LLAMA_CUBLAS "Enable CUDA support" OFF)
option(LLAMA_METAL "Enable Metal support for macOS" OFF)
option(LLAMA_OPENBLAS "Enable OpenBLAS support" OFF)

# Include FetchContent for llama.cpp
include(FetchContent)

# Fetch fmt library first (needed by spdlog)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.0.0  # Use a stable version compatible with spdlog
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
FetchContent_MakeAvailable(fmt)

# Fetch spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0  # Use latest stable version
    CMAKE_ARGS 
        -DSPDLOG_FMT_EXTERNAL=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
FetchContent_MakeAvailable(spdlog)

# Fetch FTXUI (Terminal UI Library)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v4.1.1  # Use stable version
    CMAKE_ARGS
        -DFTXUI_BUILD_DOCS=OFF
        -DFTXUI_BUILD_EXAMPLES=OFF
        -DFTXUI_BUILD_TESTS=OFF
)
FetchContent_MakeAvailable(ftxui)

# Fetch llama.cpp
FetchContent_Declare(
    llama_cpp
    GIT_REPOSITORY https://github.com/ggerganov/llama.cpp.git
    GIT_TAG c392e50  # Pinned to specific commit
)

# Configure llama.cpp build options
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)

# Pass hardware acceleration options to llama.cpp
if(LLAMA_CUBLAS)
    set(LLAMA_CUBLAS ON CACHE BOOL "Enable CUDA" FORCE)
    find_package(CUDA REQUIRED)
endif()

if(LLAMA_METAL)
    set(LLAMA_METAL ON CACHE BOOL "Enable Metal" FORCE)
endif()

if(LLAMA_OPENBLAS)
    set(LLAMA_OPENBLAS ON CACHE BOOL "Enable OpenBLAS" FORCE)
    find_package(OpenBLAS REQUIRED)
endif()

FetchContent_MakeAvailable(llama_cpp)

# Compiler warnings
if(ENABLE_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        add_compile_options(/W4 /WX)
    endif()
endif()

# Main library
add_library(llama_inference_lib
    src/LlamaInference.cpp
)

target_include_directories(llama_inference_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link with llama.cpp
target_link_libraries(llama_inference_lib
    PRIVATE
        llama
        fmt::fmt
        spdlog::spdlog
)

# Chat executable
add_executable(chat
    src/chat.cpp
)

target_link_libraries(chat
    PRIVATE
        llama_inference_lib
        llama
        fmt::fmt
        spdlog::spdlog
)

# Terminal UI Chat executable
add_executable(llm-ui
    src/terminal_ui.cpp
)

# Disable dangling-reference warning for the terminal UI component
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(llm-ui PRIVATE -Wno-dangling-reference)
endif()

target_link_libraries(llm-ui
    PRIVATE
        llama_inference_lib
        llama
        fmt::fmt
        spdlog::spdlog
        ftxui::screen
        ftxui::dom
        ftxui::component
)

# Other executables - commented out for now
# add_executable(llama_inference_v2
#     src/main.cpp
# )
# 
# target_link_libraries(llama_inference_v2
#     PRIVATE
#         llama_inference_lib
# )

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# Future dependencies to be added:
# - pybind11 for Python integration
# - nlohmann_json for JSON handling

# Installation - commented out for now
# include(GNUInstallDirs)
# install(TARGETS llama_inference_v2 llama_inference_lib
#     EXPORT llama_inference_targets
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
# )
# 
# install(DIRECTORY include/
#     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
# ) 