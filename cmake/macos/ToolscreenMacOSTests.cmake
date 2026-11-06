# Platform-independent unit tests, built and run on macOS (`ctest --test-dir out/build-macos`).
function(toolscreen_mac_unit_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${ARG_INCLUDES})
    target_compile_definitions(${name} PRIVATE NOMINMAX UNICODE _UNICODE)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND} -DTEST_BINARY=$<TARGET_FILE:${name}>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/run_listed_tests.cmake)
endfunction()

toolscreen_mac_unit_test(toolscreen_expression_parser_tests
    SOURCES tests/expression_parser_tests.cpp src/common/expression_parser.cpp)
toolscreen_mac_unit_test(toolscreen_base64_tests
    SOURCES tests/base64_tests.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/liblogger)
toolscreen_mac_unit_test(toolscreen_background_fit_layout_tests
    SOURCES tests/background_fit_layout_tests.cpp src/render/background_fit_layout.cpp)
toolscreen_mac_unit_test(toolscreen_game_state_source_tests
    SOURCES tests/game_state_source_tests.cpp src/features/game_state_source.cpp
    INCLUDES ${nlohmann_json_SOURCE_DIR}/single_include)
toolscreen_mac_unit_test(toolscreen_interactive_create_tests
    SOURCES tests/interactive_mirror_create_tests.cpp src/features/interactive_mirror_create.cpp)
