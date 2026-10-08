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
    src/core/bus.c
    src/core/m6502.c
    src/core/oric.c
    src/core/via6522.c
)

set(PICO_ORIC_WARNINGS -Wall -Wextra -Werror)

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

    enable_testing()
    add_subdirectory(test/host)
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

    add_executable(pico-oric
        src/port/board.c
        src/port/main.c
    )
    target_include_directories(pico-oric PRIVATE src/port)
    target_compile_options(pico-oric PRIVATE ${PICO_ORIC_WARNINGS})

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
    add_dependencies(pico-oric pico_oric_version)
    target_include_directories(pico-oric PRIVATE ${PICO_ORIC_GENERATED})

    # PICO_ORIC_UART=OFF is the build that ships: nothing on UART1, so GP4
    # and GP5 are free. ON, the default, is the development build, which
    # uart-log.sh and flash.sh's checks need (hardware-notes.md §2.7).
    option(PICO_ORIC_UART "Log over UART1 on GP4/GP5 (development)" ON)
    if (PICO_ORIC_UART)
        set(PICO_ORIC_UART_ON 1)
    else()
        set(PICO_ORIC_UART_ON 0)
    endif()

    # How much of the core's hot path runs from SRAM (src/core/hot.h).
    # Nothing is measured yet, so nothing moves (design.md §3.2, M2).
    set(PICO_ORIC_RAM_TIER 0 CACHE STRING "Hot-code SRAM tier, 0-2 (src/core/hot.h)")
    # The core library needs it too: that is where the marked functions are.
    target_compile_definitions(pico_oric_core PRIVATE PICO_ORIC_RAM_TIER=${PICO_ORIC_RAM_TIER})

    # clk_peri does not follow clk_sys by default; the SPI baud and the
    # audio carrier are re-applied in code as well (hardware-notes.md §3).
    target_compile_definitions(pico-oric PRIVATE
        PICO_ORIC_UART=${PICO_ORIC_UART_ON}
        PICO_ORIC_RAM_TIER=${PICO_ORIC_RAM_TIER}
        PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK=1
        # UART1 at 115200 8-N-1, TX GP4 / RX GP5 (hardware-notes.md §2.7).
        PICO_DEFAULT_UART=1
        PICO_DEFAULT_UART_TX_PIN=4
        PICO_DEFAULT_UART_RX_PIN=5
    )

    target_link_libraries(pico-oric
        pico_oric_core
        pico_stdlib
        pico_status_led
        pico_unique_id
        hardware_adc
        hardware_clocks
        hardware_vreg
    )

    pico_enable_stdio_uart(pico-oric ${PICO_ORIC_UART_ON})
    pico_enable_stdio_usb(pico-oric 0)

    pico_add_extra_outputs(pico-oric)

    # The image's size on every build (design.md §3.3, §15.2 M0).
    get_filename_component(PICO_ORIC_TOOLCHAIN_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(PICO_ORIC_SIZE arm-none-eabi-size HINTS ${PICO_ORIC_TOOLCHAIN_BIN} REQUIRED)
    add_custom_command(TARGET pico-oric POST_BUILD
        COMMAND ${PICO_ORIC_SIZE} $<TARGET_FILE:pico-oric>
        VERBATIM)
endif()
