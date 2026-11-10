add_executable(das_tests
    test_main.cpp
    test_input.cpp
)
target_link_libraries(das_tests PRIVATE das::core)
das_set_warnings(das_tests)

add_test(NAME das_tests COMMAND das_tests)
