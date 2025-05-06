include(FetchContent)

# Fetch GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# Create test executable
add_executable(llama_inference_test
    llama_inference_test.cpp
)

# Link against our library and GTest
target_link_libraries(llama_inference_test
    PRIVATE
        gmail_manager_lib
        llama
        GTest::gtest_main
        GTest::gmock
)

# Set include directories
target_include_directories(llama_inference_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${llama_cpp_SOURCE_DIR})

# Discover tests
gtest_discover_tests(llama_inference_test) 