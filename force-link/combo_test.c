// Offline test of the MAIN + MENU setup switch: gcc -Wall -o t combo_test.c combo.c && ./t
#include "combo.h"
#include <stdio.h>

static int fails;
#define CHECK(c, m) do { int ok_ = (c); printf("%-52s %s\n", m, ok_ ? "ok" : "FAIL"); if (!ok_) fails++; } while (0)
enum { SH = 0x31, MN = 0x02 };

int main(void) {
    combo_t c;
    combo_init(&c, SH, MN);
    CHECK(combo_event(&c, SH, 1, 1, 1000) == COMBO_NONE, "SHIFT down");
    CHECK(combo_event(&c, MN, 1, 1, 1100) == COMBO_NONE, "MENU down");
    CHECK(combo_tick(&c, 2000) == COMBO_NONE, "held 0.9 s: not yet");
    CHECK(combo_tick(&c, 2650) == COMBO_ARMED, "held 1.55 s: armed");
    combo_event(&c, MN, 0, 1, 2700); combo_event(&c, SH, 0, 1, 2750);
    CHECK(combo_event(&c, MN, 1, 1, 3500) == COMBO_SWITCH, "MENU again: switch");

    combo_init(&c, SH, MN);                                   // short press does nothing
    combo_event(&c, SH, 1, 1, 0); combo_event(&c, MN, 1, 1, 100); combo_event(&c, MN, 0, 1, 600);
    CHECK(combo_tick(&c, 5000) == COMBO_NONE, "released early: never armed");

    combo_init(&c, SH, MN);                                   // armed then a pad: cancel
    combo_event(&c, SH, 1, 1, 0); combo_event(&c, MN, 1, 1, 10); combo_tick(&c, 1600);
    CHECK(combo_event(&c, 0x40, 1, 0, 1700) == COMBO_CANCEL, "pad while armed: cancel");

    combo_init(&c, SH, MN);                                   // armed then timeout
    combo_event(&c, SH, 1, 1, 0); combo_event(&c, MN, 1, 1, 10); combo_tick(&c, 1600);
    CHECK(combo_tick(&c, 1600 + COMBO_CONFIRM_MS + 1) == COMBO_CANCEL, "no confirm in time: cancel");

    combo_init(&c, SH, MN);                                   // holding MENU on from the arm is not a confirm
    combo_event(&c, SH, 1, 1, 0); combo_event(&c, MN, 1, 1, 10); combo_tick(&c, 1600);
    CHECK(combo_event(&c, MN, 1, 1, 1700) == COMBO_NONE, "MENU not released yet: no switch");

    combo_init(&c, SH, MN);                                   // SHIFT still held at confirm: no switch
    combo_event(&c, SH, 1, 1, 0); combo_event(&c, MN, 1, 1, 10); combo_tick(&c, 1600);
    combo_event(&c, MN, 0, 1, 1700);
    CHECK(combo_event(&c, MN, 1, 1, 1800) == COMBO_NONE, "MAIN + MENU again: no switch");
    puts(fails ? "FAILURES" : "ALL OK");
    return fails != 0;
}
