// SHIFT + MENU setup switch, shared by the Live III addin, flsurf and the Force addin (docs/DEVICE_SWITCH.md).
// Hold SHIFT + MENU for COMBO_HOLD_MS: armed (caller blinks MENU). Then MENU alone within COMBO_CONFIRM_MS: switch.
// Any other button or pad press while armed, or the timeout, cancels.
#ifndef COMBO_H
#define COMBO_H
#include <stdint.h>

#define COMBO_HOLD_MS 1500
#define COMBO_CONFIRM_MS 4000

enum { COMBO_NONE, COMBO_ARMED, COMBO_CANCEL, COMBO_SWITCH };

typedef struct {
    uint8_t shift, menu;        // button note numbers on this controller
    int shift_down, menu_down;
    uint32_t both_since;        // ms when SHIFT and MENU were both down (0 = not)
    int armed;
    uint32_t armed_at;
    int menu_after_arm;         // MENU was released after arming (the confirm is a fresh press)
} combo_t;

void combo_init(combo_t *c, uint8_t shift_note, uint8_t menu_note);
// One button/pad event: note number, down or up, is_button (0 = pad), time in ms. Returns COMBO_*.
int combo_event(combo_t *c, uint8_t note, int down, int is_button, uint32_t now);
// Call periodically (and on every event): turns a long enough hold into ARMED, an expired arm into CANCEL.
int combo_tick(combo_t *c, uint32_t now);
#endif
