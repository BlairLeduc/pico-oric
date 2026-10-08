# Host tests (design.md §13). Every one of these builds src/core/ with the
# system compiler and no Pico SDK. One binary per area; exit 77 is a skip.

set(PICO_ORIC_HOST_TESTS
    test_m6502_behaviour
    test_m6502_cycles
    test_m6502_decimal
    test_skeleton
    test_via6522
)

foreach(t ${PICO_ORIC_HOST_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE pico_oric_core)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${t} PRIVATE ${PICO_ORIC_WARNINGS})
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()

# The Dormann and Clark suites need binaries the repository does not ship
# (§5.4); without them the test reports as skipped, not as passing.
# PICO_ORIC_TEST_ROMS in the environment wins; otherwise the test looks
# where tools/fetch-test-suites.sh puts them by default.
add_executable(test_m6502_functional test_m6502_functional.c)
target_link_libraries(test_m6502_functional PRIVATE pico_oric_core)
target_include_directories(test_m6502_functional PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_m6502_functional PRIVATE ${PICO_ORIC_WARNINGS})
target_compile_definitions(test_m6502_functional PRIVATE
    PICO_ORIC_DEFAULT_TEST_ROMS="${CMAKE_SOURCE_DIR}/test/suites")
add_test(NAME test_m6502_functional COMMAND test_m6502_functional)
set_tests_properties(test_m6502_functional PROPERTIES SKIP_RETURN_CODE 77)
