cmake_minimum_required(VERSION 3.14)
project(gmail_manager_v2 VERSION 0.1.0
    DESCRIPTION "Gmail Manager with LLM Integration - Version 2"
    LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Basic options
option(BUILD_TESTS "Build test suite" ON)
option(ENABLE_WARNINGS "Enable warnings" ON)

# Hardware acceleration options
option(LLAMA_CUBLAS "Enable CUDA support" OFF)
option(LLAMA_METAL "Enable Metal support for macOS" OFF)
option(LLAMA_OPENBLAS "Enable OpenBLAS support" OFF)

# Include FetchContent for llama.cpp
include(FetchContent)

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
add_library(gmail_manager_lib
    src/LlamaInference.cpp
)

target_include_directories(gmail_manager_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link with llama.cpp
target_link_libraries(gmail_manager_lib
    PRIVATE
        llama
)

# Chat executable
add_executable(chat
    src/chat.cpp
)

target_link_libraries(chat
    PRIVATE
        gmail_manager_lib
        llama
)

# Other executables - commented out for now
# add_executable(gmail_manager_v2
#     src/main.cpp
# )
# 
# target_link_libraries(gmail_manager_v2
#     PRIVATE
#         gmail_manager_lib
# )

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# Future dependencies to be added:
# - pybind11 for Python integration
# - nlohmann_json for JSON handling
# - FTXUI for the terminal UI

# Installation - commented out for now
# include(GNUInstallDirs)
# install(TARGETS gmail_manager_v2 gmail_manager_lib
#     EXPORT gmail_manager_targets
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
# )
# 
# install(DIRECTORY include/
#     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
# ) 