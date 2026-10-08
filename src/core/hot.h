/* hot.h — which of the core's functions run from SRAM (design.md §3.2,
 * hardware-notes.md §9.2, §9.8).
 *
 * On the device, flash code is fetched through a 16 KiB XIP cache that
 * both cores share, and an interpreter alone is larger than that. The
 * SDK's linker script copies every `.time_critical*` input section into
 * SRAM at boot, so naming a function's section is all it takes to move
 * it. That is the SDK's convention, not an SDK dependency: nothing here
 * includes a Pico header.
 *
 * Moves come in tiers, and PICO_ORIC_RAM_TIER says how many are taken.
 * The firmware's CMake defaults to tier 2. M2's bench, on one core, found
 * tier 2 within 2 % of tier 0 and tier 1 slower; with core 1 presenting,
 * M7 measured tier 2 at 1.25-1.58x tier 0 on every workload (design.md
 * §3.2, hardware-notes.md §9.8). A host build leaves it at 0 and gets
 * ordinary functions.
 *
 *   ORIC_HOT1(name)  what the interpreter calls out to: the bus slow path,
 *                    the VIA, the AY.
 *   ORIC_HOT2(name)  the interpreter and its loop.
 *
 * Whole files are the wrong unit (hardware-notes.md §9.2): mark the
 * function, not the module, and check the symbol moved from 0x1... to
 * 0x2... with arm-none-eabi-nm after the build (hardware-notes.md §9.8).
 */
#ifndef PICO_ORIC_HOT_H
#define PICO_ORIC_HOT_H

#ifndef PICO_ORIC_RAM_TIER
#define PICO_ORIC_RAM_TIER 0
#endif

#define ORIC_IN_RAM_(name) __attribute__((section(".time_critical.oric_" #name))) name

#if PICO_ORIC_RAM_TIER >= 1
#define ORIC_HOT1(name) ORIC_IN_RAM_(name)
#else
#define ORIC_HOT1(name) name
#endif

#if PICO_ORIC_RAM_TIER >= 2
#define ORIC_HOT2(name) ORIC_IN_RAM_(name)
#else
#define ORIC_HOT2(name) name
#endif

#endif /* PICO_ORIC_HOT_H */
