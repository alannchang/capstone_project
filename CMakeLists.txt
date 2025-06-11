cmake_minimum_required(VERSION 3.14)
project(MaiMail)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ───── Include Paths ───────────────────────────────────────
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)

# ───── Dependencies via FetchContent ───────────────────────
include(FetchContent)

# Fetch llama.cpp
FetchContent_Declare(
    llama_cpp
    GIT_REPOSITORY https://github.com/ggerganov/llama.cpp.git
    GIT_TAG b5306  # Pinned to specific commit 
)

# Configure llama.cpp build options
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)

# --- Add SQLite3 ---
# NOTE: We are intentionally using the deprecated FetchContent_Populate here.
# The modern FetchContent_MakeAvailable can cause issues with non-CMake 
# projects like the SQLite amalgamation, leading to build failures. This older 
# pattern is more reliable in this case, even if it produces a developer warning.
FetchContent_Declare(
    sqlite3
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3460000.zip
    URL_HASH SHA256=712a7d09d2a22652fb06a49af516e051979a3984adb067da86760e60ed51a7f5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(sqlite3)
if(NOT sqlite3_POPULATED)
    FetchContent_Populate(sqlite3)
    # SQLite amalgamation files are extracted directly to the source directory
    add_library(sqlite3_static STATIC "${sqlite3_SOURCE_DIR}/sqlite3.c")
    target_include_directories(sqlite3_static PUBLIC "${sqlite3_SOURCE_DIR}")
endif()

FetchContent_MakeAvailable(llama_cpp ftxui json httplib)

# --- Add FAISS as subdirectory ---
# This approach gives us more control over the build configuration
FetchContent_Declare(
    faiss
    GIT_REPOSITORY https://github.com/facebookresearch/faiss.git
    GIT_TAG v1.8.0
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(faiss)
if(NOT faiss_POPULATED)
    FetchContent_Populate(faiss)
    
    # Configure FAISS options before adding as subdirectory
    set(FAISS_ENABLE_GPU OFF CACHE BOOL "" FORCE)
    set(FAISS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FAISS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FAISS_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(FAISS_OPT_LEVEL generic CACHE STRING "" FORCE)
    
    # Try to disable BLAS requirement or use internal implementations
    set(FAISS_ENABLE_C_API ON CACHE BOOL "" FORCE)
    
    # Add FAISS as subdirectory with custom error handling
    add_subdirectory(${faiss_SOURCE_DIR} ${faiss_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# ───── Sources ─────────────────────────────────────────────
file(GLOB SRC_FILES src/*.cpp)

# Exclude test files from main executable
list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/test_database.cpp)

add_executable(chat ${SRC_FILES})

# Create test executable for database testing
add_executable(test_database src/test_database.cpp src/DatabaseManager.cpp)

# ───── Linking ─────────────────────────────────────────────
target_link_libraries(chat PRIVATE
    common llama ggml
    ftxui::screen ftxui::dom ftxui::component
    nlohmann_json::nlohmann_json
    httplib::httplib
    sqlite3_static
    faiss
)

target_link_libraries(test_database PRIVATE
    nlohmann_json::nlohmann_json
    sqlite3_static
)

target_include_directories(chat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(test_database PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
