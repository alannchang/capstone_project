cmake_minimum_required(VERSION 3.10)
project(MaiMail)

set(CMAKE_CXX_STANDARD 17)
set(LLAMA_BUILD_COMMON ON)

# ───── Subdirectories ──────────────────────────────────────
add_subdirectory(llama.cpp)

# ───── Include Paths ───────────────────────────────────────
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)

# ───── Gmail Runtime Dependencies ──────────────────────────
file(COPY ${CMAKE_SOURCE_DIR}/gmail-api/api.py DESTINATION ${CMAKE_BINARY_DIR}/runtime-deps)
file(COPY ${CMAKE_SOURCE_DIR}/gmail-api/runtime-deps/ DESTINATION ${CMAKE_BINARY_DIR}/runtime-deps)

# ───── Dependencies via FetchContent ───────────────────────
include(FetchContent)

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.0.2
)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.13.6
)

FetchContent_MakeAvailable(ftxui json pybind11)

# ───── Sources ─────────────────────────────────────────────
file(GLOB SRC_FILES src/*.cpp)

add_executable(chat ${SRC_FILES})

# ───── Linking ─────────────────────────────────────────────
target_link_libraries(chat PRIVATE
    common llama ggml
    ftxui::screen ftxui::dom ftxui::component
    nlohmann_json::nlohmann_json
    pybind11::embed
)

target_include_directories(chat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
