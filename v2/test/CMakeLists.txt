enable_testing()

add_executable(unit_tests
    # Add test source files here
)

target_link_libraries(unit_tests
    PRIVATE
        gmail_manager_lib
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(unit_tests) 