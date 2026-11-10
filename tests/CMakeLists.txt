add_executable(das_tests
    test_main.cpp
    test_input.cpp
    test_data_codecs.cpp
    test_game_files.cpp
    test_world.cpp
    test_util.cpp
)
target_link_libraries(das_tests PRIVATE das::core)
das_set_warnings(das_tests)

add_test(NAME das_tests COMMAND das_tests)
