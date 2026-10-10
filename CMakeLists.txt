# pico-oric — one source tree, two targets (design.md §4.1):
#   - the UF2 for the PicoCalc (default)
#   - a host test build that compiles src/core/ with the system compiler
#     and no Pico SDK, behind -DPICO_ORIC_HOST=ON, run under CTest.
#
# The core building clean under -Wall -Wextra -Werror on both toolchains
# is the mechanism that keeps SDK dependencies from leaking downwards.

cmake_minimum_required(VERSION 3.18)

option(PICO_ORIC_HOST "Build the portable core and its host tests, with no Pico SDK" OFF)

# Every fixed capacity lives in src/core/config.h; nothing here duplicates one.
set(PICO_ORIC_CORE_SOURCES
    src/core/ay8912.c
    src/core/pcm.c
    src/core/bus.c
    src/core/cassette.c
    src/core/font.c
    src/core/font_fallback.c
    src/core/keymap_picocalc.c
    src/core/keymatrix.c
    src/core/m6502.c
    src/core/oric.c
    src/core/romset.c
    src/core/settings.c
    src/core/shot.c
    src/core/sha1.c
    src/core/snappool.c
    src/core/snapshot.c
    src/core/status.c
    src/core/tap.c
    src/core/tape.c
    src/core/ula.c
    src/core/via6522.c
)

set(PICO_ORIC_WARNINGS -Wall -Wextra -Werror)

# M2's workloads: portable like the core, run by a host test and by the
# bench firmware (design.md §15.2 M2).
function(pico_oric_bench_library)
    add_library(pico_oric_bench STATIC src/bench/bench.c)
    target_include_directories(pico_oric_bench PUBLIC src/bench)
    target_link_libraries(pico_oric_bench PUBLIC pico_oric_core)
    target_compile_options(pico_oric_bench PRIVATE ${PICO_ORIC_WARNINGS})
endfunction()

# The core as a library both targets link. Headers only while it has no
# sources; a STATIC library, built with our warnings, once it has.
function(pico_oric_core_library)
    if (PICO_ORIC_CORE_SOURCES)
        add_library(pico_oric_core STATIC ${PICO_ORIC_CORE_SOURCES})
        target_include_directories(pico_oric_core PUBLIC src/core)
        target_compile_options(pico_oric_core PRIVATE ${PICO_ORIC_WARNINGS})
    else()
        add_library(pico_oric_core INTERFACE)
        target_include_directories(pico_oric_core INTERFACE src/core)
    endif()
endfunction()

if (PICO_ORIC_HOST)
    project(pico_oric_host C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    pico_oric_core_library()
    pico_oric_bench_library()

    # Our half of the trace diff against Oricutron (design.md §13.4).
    add_executable(oric-trace tools/trace/oric-trace.c)
    target_link_libraries(oric-trace PRIVATE pico_oric_core)
    target_include_directories(oric-trace PRIVATE tools/trace)
    target_compile_options(oric-trace PRIVATE ${PICO_ORIC_WARNINGS})

    enable_testing()
    add_subdirectory(test/host)

    # M12's corpus run: archive tapes loaded and run on the guest harness
    # (design.md §15, M12; tools/corpus-run.sh).
    add_executable(oric-corpus tools/corpus/oric-corpus.c)
    target_link_libraries(oric-corpus PRIVATE pico_oric_guest)
    target_compile_options(oric-corpus PRIVATE ${PICO_ORIC_WARNINGS})
else()
    # One pico2 image for every RP2350 board (design.md §3.1).
    if (NOT DEFINED PICO_BOARD)
        set(PICO_BOARD pico2)
    endif()
    if (NOT DEFINED PICO_PLATFORM AND PICO_BOARD STREQUAL "pico2")
        set(PICO_PLATFORM rp2350)
    endif()

    include(cmake/pico_sdk_import.cmake)

    project(pico_oric C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    pico_sdk_init()

    pico_oric_core_library()
    pico_oric_bench_library()

    # The firmware's version, git describe --always --dirty, for the
    # banner. A target that always runs, so it is taken at build time; the
    # header only changes when the text does.
    set(PICO_ORIC_GENERATED ${CMAKE_BINARY_DIR}/generated)
    add_custom_target(pico_oric_version ALL
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUT=${PICO_ORIC_GENERATED}/pico_oric_version.h
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake
        BYPRODUCTS ${PICO_ORIC_GENERATED}/pico_oric_version.h
        COMMENT "pico-oric: version from git describe"
        VERBATIM)

    # PICO_ORIC_UART=OFF is the build that ships: nothing on UART1, so GP4
    # and GP5 are free. ON, the default, is the development build, which
    # uart-log.sh and flash.sh's checks need (hardware-notes.md §2.7).
    option(PICO_ORIC_UART "Log over UART1 on GP4/GP5 (development)" ON)
    if (PICO_ORIC_UART)
        set(PICO_ORIC_UART_ON 1)
    else()
        set(PICO_ORIC_UART_ON 0)
    endif()

    # PICO_ORIC_AUDIO=OFF paces on the microsecond timer and leaves the PWM
    # pins alone, the control for audio's cost; ON, the default, paces on
    # the audio queue (design.md §8.4, EL §6.3).
    option(PICO_ORIC_AUDIO "Sound on GP26/GP27, and pacing on its queue" ON)
    if (PICO_ORIC_AUDIO)
        set(PICO_ORIC_AUDIO_ON 1)
    else()
        set(PICO_ORIC_AUDIO_ON 0)
    endif()

    # How much of the core's hot path runs from SRAM (src/core/hot.h),
    # measured in M2 and M7 (design.md §3.2): tier 2 from M7. A build
    # directory configured before keeps its cached 0 until it is passed
    # once (hardware-notes.md §9.8).
    set(PICO_ORIC_RAM_TIER 2 CACHE STRING "Hot-code SRAM tier, 0-2 (src/core/hot.h)")
    # The core library needs it too: that is where the marked functions are.
    target_compile_definitions(pico_oric_core PRIVATE PICO_ORIC_RAM_TIER=${PICO_ORIC_RAM_TIER})

    # The tape trap's check in the run loop (design.md §10.3); OFF is the
    # control M10 measures its cost against, and no tape is served.
    option(PICO_ORIC_TAPE "The tape trap's check in the run loop" ON)
    if (PICO_ORIC_TAPE)
        target_compile_definitions(pico_oric_core PRIVATE PICO_ORIC_TAPE_TRAP=1)
    else()
        target_compile_definitions(pico_oric_core PRIVATE PICO_ORIC_TAPE_TRAP=0)
    endif()

    get_filename_component(PICO_ORIC_TOOLCHAIN_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(PICO_ORIC_SIZE arm-none-eabi-size HINTS ${PICO_ORIC_TOOLCHAIN_BIN} REQUIRED)

    # What every image shares: the port's include path, the version, the
    # UART, the tier, the clocks, and its size printed on every build.
    function(pico_oric_firmware target)
        target_include_directories(${target} PRIVATE src/port ${PICO_ORIC_GENERATED})
        target_compile_options(${target} PRIVATE ${PICO_ORIC_WARNINGS})
        add_dependencies(${target} pico_oric_version)

        # clk_peri does not follow clk_sys by default; the SPI baud and the
        # audio carrier are re-applied in code as well (hardware-notes.md §3).
        target_compile_definitions(${target} PRIVATE
            PICO_ORIC_UART=${PICO_ORIC_UART_ON}
            PICO_ORIC_RAM_TIER=${PICO_ORIC_RAM_TIER}
            PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK=1
            # UART1 at 115200 8-N-1, TX GP4 / RX GP5 (hardware-notes.md §2.7).
            PICO_DEFAULT_UART=1
            PICO_DEFAULT_UART_TX_PIN=4
            PICO_DEFAULT_UART_RX_PIN=5
        )

        target_link_libraries(${target}
            pico_oric_core
            pico_stdlib
            pico_unique_id
            hardware_adc
            hardware_clocks
            hardware_vreg
        )

        pico_enable_stdio_uart(${target} ${PICO_ORIC_UART_ON})
        pico_enable_stdio_usb(${target} 0)

        pico_add_extra_outputs(${target})

        # The image's size on every build (design.md §3.3, §15.2 M0).
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${PICO_ORIC_SIZE} $<TARGET_FILE:${target}>
            VERBATIM)
    endfunction()

    # FatFs is not in the tree (THIRD-PARTY.md). The SDK carries a copy for
    # TinyUSB, and ff.h includes "ffconf.h" from its own directory, so the
    # files are copied into the build tree beside our src/port/fatfs/ffconf.h
    # rather than compiled in place (which would find TinyUSB's config).
    set(PICO_ORIC_FATFS_SRC ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source)
    set(PICO_ORIC_FATFS_DIR ${CMAKE_BINARY_DIR}/fatfs)
    foreach(f ff.c ff.h ffunicode.c diskio.h)
        if (NOT EXISTS ${PICO_ORIC_FATFS_SRC}/${f})
            message(FATAL_ERROR "pico-oric: FatFs ${f} not found in ${PICO_ORIC_FATFS_SRC}")
        endif()
        configure_file(${PICO_ORIC_FATFS_SRC}/${f} ${PICO_ORIC_FATFS_DIR}/${f} COPYONLY)
    endforeach()
    configure_file(src/port/fatfs/ffconf.h ${PICO_ORIC_FATFS_DIR}/ffconf.h COPYONLY)

    add_library(pico_oric_fatfs STATIC
        ${PICO_ORIC_FATFS_DIR}/ff.c
        ${PICO_ORIC_FATFS_DIR}/ffunicode.c
    )
    target_include_directories(pico_oric_fatfs PUBLIC ${PICO_ORIC_FATFS_DIR})
    # Third-party code: built as it comes, without our -Werror.

    # The emulator (design.md §15.2 M7).
    add_executable(pico-oric
        src/port/audio.c
        src/port/board.c
        src/port/card.c
        src/port/core0.c
        src/port/core1.c
        src/port/diskio.c
        src/port/display.c
        src/port/handoff.c
        src/port/kbd.c
        src/port/lcd.c
        src/port/log.c
        src/port/main.c
        src/port/menu.c
        src/port/park.c
        src/port/roms.c
        src/port/sd.c
        src/port/settingsio.c
        src/port/shotio.c
        src/port/snapio.c
        src/port/southbridge.c
        src/port/storage.c
        src/port/tapeio.c
        src/port/textpage.c
    )
    pico_oric_firmware(pico-oric)

    # The machine a build boots (design.md §4.1), over the Atmos 48K
    # default: PICO_ORIC_BOOT_ROM 10 or 11, PICO_ORIC_BOOT_RAM 16 or 48.
    # Each in its own build directory, which keeps its cached options
    # (EL §13.1).
    set(PICO_ORIC_BOOT_ROM "" CACHE STRING "BASIC ROM to boot: 10 or 11, empty for the default")
    set(PICO_ORIC_BOOT_RAM "" CACHE STRING "RAM to boot: 16 or 48, empty for the default")
    if (PICO_ORIC_BOOT_ROM)
        if (NOT PICO_ORIC_BOOT_ROM MATCHES "^(10|11)$")
            message(FATAL_ERROR "PICO_ORIC_BOOT_ROM is 10 or 11, not ${PICO_ORIC_BOOT_ROM}")
        endif()
        target_compile_definitions(pico-oric PRIVATE PICO_ORIC_BOOT_ROM=${PICO_ORIC_BOOT_ROM})
    endif()
    if (PICO_ORIC_BOOT_RAM)
        if (NOT PICO_ORIC_BOOT_RAM MATCHES "^(16|48)$")
            message(FATAL_ERROR "PICO_ORIC_BOOT_RAM is 16 or 48, not ${PICO_ORIC_BOOT_RAM}")
        endif()
        target_compile_definitions(pico-oric PRIVATE PICO_ORIC_BOOT_RAM=${PICO_ORIC_BOOT_RAM})
    endif()
    # A tape in the deck at boot, over the file's boot_tape: a name in
    # /oric/tapes/ or a path (design.md §10.7).
    set(PICO_ORIC_BOOT_TAPE "" CACHE STRING "tape in the deck at boot, empty for the file's")
    if (PICO_ORIC_BOOT_TAPE)
        target_compile_definitions(pico-oric PRIVATE PICO_ORIC_BOOT_TAPE="${PICO_ORIC_BOOT_TAPE}")
    endif()

    target_compile_definitions(pico-oric PRIVATE PICO_ORIC_AUDIO=${PICO_ORIC_AUDIO_ON})
    target_link_libraries(pico-oric
        pico_oric_fatfs
        hardware_dma
        hardware_i2c
        hardware_irq
        hardware_pwm
        hardware_spi
        hardware_sync
        pico_multicore
    )

    # M2's bench: the 6502 alone, timed on the basic program and on
    # Dormann's test when tools/fetch-test-suites.sh has fetched it
    # (design.md §15.2 M2).
    add_executable(pico-oric-bench
        src/port/board.c
        src/port/bench_main.c
    )
    pico_oric_firmware(pico-oric-bench)
    target_link_libraries(pico-oric-bench pico_oric_bench)
    set(PICO_ORIC_DORMANN ${CMAKE_CURRENT_SOURCE_DIR}/test/suites/6502_functional_test.bin)
    if (EXISTS ${PICO_ORIC_DORMANN})
        target_sources(pico-oric-bench PRIVATE src/port/bench_dormann.S)
        set_source_files_properties(src/port/bench_dormann.S PROPERTIES
            OBJECT_DEPENDS ${PICO_ORIC_DORMANN})
        target_compile_definitions(pico-oric-bench PRIVATE
            PICO_ORIC_BENCH_DORMANN=1
            PICO_ORIC_DORMANN_PATH="${PICO_ORIC_DORMANN}")
    else()
        message(STATUS "pico-oric-bench: no ${PICO_ORIC_DORMANN}; the basic workload only")
        target_compile_definitions(pico-oric-bench PRIVATE PICO_ORIC_BENCH_DORMANN=0)
    endif()
endif()
