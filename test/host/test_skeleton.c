/* test_skeleton.c — M0 (design.md §15.2). Compiles the core's headers
 * with the system compiler and no Pico SDK, so an SDK #include in one of
 * them fails the host build, and checks config.h's sizes against each
 * other. */

#include "config.h"
#include "hot.h"
#include "test_util.h"

/* A host build is tier 0: the macro must leave an ordinary function. */
static int ORIC_HOT2(hot_identity)(int x) { return x; }

int main(void) {
    CHECK(hot_identity(42) == 42, "ORIC_HOT2 changed the function");

    CHECK(ORIC_PAGE_COUNT * ORIC_PAGE_SIZE == ORIC_ADDR_SPACE,
          "%u pages of %u bytes", ORIC_PAGE_COUNT, ORIC_PAGE_SIZE);
    CHECK(ORIC_ROM_BASE % ORIC_PAGE_SIZE == 0 && ORIC_ROM_SIZE % ORIC_PAGE_SIZE == 0,
          "the ROM does not fill whole pages of the page table (§6.1)");
    CHECK(ORIC_ROM_BASE + ORIC_ROM_SIZE == ORIC_ADDR_SPACE,
          "the ROM does not end at #FFFF, where the vectors are (§2.2)");

    TEST_DONE();
}
