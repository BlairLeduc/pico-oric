/* handoff.c — what the two cores share (design.md §4.3, §4.4). */

#include "handoff.h"

volatile core1_stats_t g_c1 = { .sb_version = -1, .battery = -1, .temp_c = INT32_MIN };
bringup_t g_bringup;
board_info_t g_board;
