#include "combo.h"
#include <string.h>

void combo_init(combo_t *c, uint8_t shift_note, uint8_t menu_note) {
    memset(c, 0, sizeof *c);
    c->shift = shift_note; c->menu = menu_note;
}

int combo_tick(combo_t *c, uint32_t now) {
    if (c->armed) {
        if (now - c->armed_at > COMBO_CONFIRM_MS) { c->armed = 0; return COMBO_CANCEL; }
        return COMBO_NONE;
    }
    if (c->both_since && now - c->both_since >= COMBO_HOLD_MS) {
        c->armed = 1; c->armed_at = now; c->menu_after_arm = 0; c->both_since = 0;
        return COMBO_ARMED;
    }
    return COMBO_NONE;
}

int combo_event(combo_t *c, uint8_t note, int down, int is_button, uint32_t now) {
    int r = combo_tick(c, now);
    if (r != COMBO_NONE) return r;
    if (is_button && note == c->shift) c->shift_down = down;
    if (is_button && note == c->menu) c->menu_down = down;
    if (c->armed) {
        if (is_button && note == c->menu) {
            if (!down) { c->menu_after_arm = 1; return COMBO_NONE; }
            if (c->menu_after_arm && !c->shift_down) { c->armed = 0; return COMBO_SWITCH; }
            return COMBO_NONE;
        }
        if (is_button && note == c->shift) return COMBO_NONE;   // letting go of SHIFT is part of the gesture
        if (down) { c->armed = 0; return COMBO_CANCEL; }
        return COMBO_NONE;
    }
    c->both_since = (c->shift_down && c->menu_down) ? (c->both_since ? c->both_since : (now ? now : 1)) : 0;
    return COMBO_NONE;
}
