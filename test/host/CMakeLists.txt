# Host tests (design.md §13). Every one of these builds src/core/ with the
# system compiler and no Pico SDK. One binary per area; exit 77 is a skip.

set(PICO_ORIC_HOST_TESTS
    test_audio
    test_ay8912
    test_bus
    test_field
    test_keymap
    test_m6502_behaviour
    test_m6502_cycles
    test_m6502_decimal
    test_skeleton
    test_snappool
    test_status
    test_ula
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

# M2's workloads (design.md §15.2 M2): the basic program always, Dormann's
# slice when the suite has been fetched.
add_executable(test_bench test_bench.c)
target_link_libraries(test_bench PRIVATE pico_oric_bench)
target_include_directories(test_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_bench PRIVATE ${PICO_ORIC_WARNINGS})
add_test(NAME test_bench_basic COMMAND test_bench)
add_test(NAME test_bench_dormann COMMAND test_bench
    ${CMAKE_SOURCE_DIR}/test/suites/6502_functional_test.bin)
set_tests_properties(test_bench_basic test_bench_dormann PROPERTIES SKIP_RETURN_CODE 77)

# The harness for the tests that run the real ROMs (§13.3). It finds them
# by SHA-1 in PICO_ORIC_ROMS, else the repository's gitignored roms/.
add_library(pico_oric_guest STATIC guest.c)
target_link_libraries(pico_oric_guest PUBLIC pico_oric_core)
target_include_directories(pico_oric_guest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(pico_oric_guest PRIVATE ${PICO_ORIC_WARNINGS})
target_compile_definitions(pico_oric_guest PRIVATE
    PICO_ORIC_DEFAULT_ROMS="${CMAKE_SOURCE_DIR}/roms")

foreach(t test_audio_rom test_boot test_font test_keyboard test_romset)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE pico_oric_guest)
    target_compile_options(${t} PRIVATE ${PICO_ORIC_WARNINGS})
    target_compile_definitions(${t} PRIVATE PICO_ORIC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
target_link_libraries(test_audio_rom PRIVATE m)

# Our own test ROM (§13.3), assembled when cc65 is on the path, as CI
# installs it. Without it test_test_rom reports skipped.
find_program(PICO_ORIC_CA65 ca65)
find_program(PICO_ORIC_LD65 ld65)
set(PICO_ORIC_TEST_ROM ${CMAKE_CURRENT_BINARY_DIR}/oric_test_rom.bin)
if (PICO_ORIC_CA65 AND PICO_ORIC_LD65)
    set(asm ${CMAKE_SOURCE_DIR}/test/asm)
    add_custom_command(
        OUTPUT ${PICO_ORIC_TEST_ROM}
        COMMAND ${PICO_ORIC_CA65} -o ${CMAKE_CURRENT_BINARY_DIR}/oric_test_rom.o ${asm}/oric_test_rom.s
        COMMAND ${PICO_ORIC_LD65} -C ${asm}/oric_test_rom.cfg -o ${PICO_ORIC_TEST_ROM}
                ${CMAKE_CURRENT_BINARY_DIR}/oric_test_rom.o
        DEPENDS ${asm}/oric_test_rom.s ${asm}/oric_test_rom.cfg
        COMMENT "ca65: oric_test_rom.bin"
        VERBATIM)
    add_custom_target(pico_oric_test_rom ALL DEPENDS ${PICO_ORIC_TEST_ROM})
else()
    message(STATUS "pico-oric: ca65/ld65 not found; test_test_rom will skip")
endif()

add_executable(test_test_rom test_test_rom.c)
target_link_libraries(test_test_rom PRIVATE pico_oric_core)
target_include_directories(test_test_rom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_test_rom PRIVATE ${PICO_ORIC_WARNINGS})
target_compile_definitions(test_test_rom PRIVATE PICO_ORIC_TEST_ROM="${PICO_ORIC_TEST_ROM}")
add_test(NAME test_test_rom COMMAND test_test_rom)
set_tests_properties(test_test_rom PROPERTIES SKIP_RETURN_CODE 77)

# The golden images (design.md §7.7), drawn with the fallback font so CI
# runs them with no ROM.
add_executable(test_golden test_golden.c)
target_link_libraries(test_golden PRIVATE pico_oric_core)
target_include_directories(test_golden PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_golden PRIVATE ${PICO_ORIC_WARNINGS})
target_compile_definitions(test_golden PRIVATE
    PICO_ORIC_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME test_golden COMMAND test_golden)

# The port's audio path, heard (design.md §8.5): src/port/audio.c itself,
# built against sdk_sim/'s stand-ins for the SDK, whose DMA reads the ring
# as the hardware does.
add_executable(test_audio_port test_audio_port.c ${CMAKE_SOURCE_DIR}/src/port/audio.c
    sdk_sim/sdk_sim.c)
target_link_libraries(test_audio_port PRIVATE pico_oric_core m)
target_include_directories(test_audio_port BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk_sim
    ${CMAKE_SOURCE_DIR}/src/port ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_audio_port PRIVATE ${PICO_ORIC_WARNINGS})
add_test(NAME test_audio_port COMMAND test_audio_port)
