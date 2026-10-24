#include "flparse.h"
#include <stdio.h>
#include <string.h>

// Button numbers = note / CC numbers used by the Force controller (from KikGen's Force headers).
static const struct { int n; const char *name; } BTN[] = {
    {0,"NAVIGATE"},{1,"KNOBS"},{2,"MENU"},{3,"MATRIX"},{4,"NOTE"},{5,"MASTER"},{9,"CLIP"},{11,"MIXER"},
    {35,"LOAD"},{36,"SAVE"},{37,"EDIT"},{38,"DELETE"},{0x31,"SHIFT"},{52,"SELECT"},{0x35,"TAP_TEMPO"},
    {54,"PLUS"},{55,"MINUS"},{56,"LAUNCH_1"},{57,"LAUNCH_2"},{58,"LAUNCH_3"},{59,"LAUNCH_4"},
    {60,"LAUNCH_5"},{61,"LAUNCH_6"},{62,"LAUNCH_7"},{63,"LAUNCH_8"},{67,"UNDO"},{73,"REC"},{81,"STOP"},
    {82,"PLAY"},{91,"MUTE"},{92,"SOLO"},{93,"REC_ARM"},{94,"CLIP_STOP"},{95,"STOP_ALL"},
    {0x6F,"ENCODER_PUSH"},{0x70,"UP"},{0x71,"DOWN"},{0x72,"LEFT"},{0x73,"RIGHT"},{116,"LAUNCH"},
    {117,"STEP_SEQ"},{118,"ARP"},{122,"COPY"},{123,"ASSIGN_A"},{124,"ASSIGN_B"},
    {96,"TRACK_SEL_1"},{97,"COLUMN_2"},{98,"COLUMN_3"},{99,"COLUMN_4"},{100,"COLUMN_5"},{101,"COLUMN_6"},
    {102,"TRACK_SEL_7"},{103,"COLUMN_8"},{41,"TRACK_ARM_1"},{42,"MUTE_PAD_2"},{43,"MUTE_PAD_3"},
    {44,"TRACK_ARM_4"},{45,"MUTE_PAD_5"},{46,"MUTE_PAD_6"},{47,"MUTE_PAD_7"},{48,"MUTE_PAD_8"},
};
const char *fl_button_name(int n) {
    for (size_t i = 0; i < sizeof BTN / sizeof *BTN; i++) if (BTN[i].n == n) return BTN[i].name;
    return NULL;
}

void fl_parser_init(fl_parser *p) { memset(p, 0, sizeof *p); }

static int msg_len(uint8_t s) {
    switch (s & 0xF0) { case 0xC0: case 0xD0: return 1; case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2; }
    return 0;
}

// Interpretation (VERIFIED on a Force 2026-10-15 unless noted):
//  * pads: ch9 (status 0x99/0x89/0xA9) notes 54..117, note = 54 + row*8 + col, row 0 = top
//  * buttons: notes on ch0, any port, names from the table (NOTE=4, MIXER=11, LAUNCH=116 seen)
//  * Q-link touch: ch0 note 0x53..0x5A
//  * Q-link knobs: CC 0x10..0x17 carry a running count since touch-down (1,2,3..);
//    step = (value - previous) mod 128 as signed 7-bit; counter resets on touch
static void emit(fl_parser *p, int port, fl_cb cb, void *ctx) {
    char line[128]; uint8_t s = p->status & 0xF0, a = p->d[0], b = p->d[1];
    int ch = p->status & 0x0F;
    if (s == 0x90 || s == 0x80) {
        int down = (s == 0x90 && b > 0);
        if (ch == 9 && a >= 54 && a <= 117) snprintf(line, sizeof line, "PAD n=%d row=%d col=%d %s vel=%d", a, (a - 54) / 8, (a - 54) % 8, down ? "down" : "up", b);
        else if (a >= 0x53 && a <= 0x5A) { if (down) p->knob_prev[a - 0x53] = 0; snprintf(line, sizeof line, "TOUCH knob=%d %s", a - 0x53 + 1, down ? "down" : "up"); }
        else if (fl_button_name(a)) snprintf(line, sizeof line, "BTN %s %s vel=%d", fl_button_name(a), down ? "down" : "up", b);
        else snprintf(line, sizeof line, "NOTE port=%d ch=%d n=%d %s vel=%d", port, ch, a, down ? "down" : "up", b);
    } else if (s == 0xB0) {
        if (a >= 0x10 && a <= 0x17) {
            int k = a - 0x10, d = (b - p->knob_prev[k]) & 127;
            if (d > 63) d -= 128;
            p->knob_prev[k] = b;
            snprintf(line, sizeof line, "KNOB k=%d delta=%d", k + 1, d);
        } else if (a == 100) {
            snprintf(line, sizeof line, "ENCODER delta=%d", b < 64 ? b : b - 128);
        } else snprintf(line, sizeof line, "CC port=%d ch=%d cc=%d val=%d", port, ch, a, b);
    } else if (s == 0xA0) {
        if (ch == 9 && a >= 54 && a <= 117) snprintf(line, sizeof line, "PADAT n=%d pressure=%d", a, b);
        else snprintf(line, sizeof line, "AT ch=%d n=%d pressure=%d", ch, a, b);
    }
    else if (s == 0xE0) snprintf(line, sizeof line, "BEND ch=%d val=%d", ch, ((b << 7) | a) - 8192);
    else snprintf(line, sizeof line, "MIDI port=%d status=%02X %02X %02X", port, p->status, a, b);
    cb(ctx, line);
}

void fl_parser_feed(fl_parser *p, int port, const uint8_t *buf, size_t n, fl_cb cb, void *ctx) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = buf[i];
        if (c >= 0xF8) continue;                       // realtime
        if (c == 0xF0) { p->in_sysex = 1; p->sx_len = 1; continue; }
        if (c == 0xF7) {
            if (p->in_sysex) { char l[48]; snprintf(l, sizeof l, "SYSEX port=%d len=%zu", port, p->sx_len + 1); cb(ctx, l); }
            p->in_sysex = 0; continue;
        }
        if (c & 0x80) { p->in_sysex = 0; p->status = c; p->need = msg_len(c); p->have = 0; continue; }
        if (p->in_sysex) { p->sx_len++; continue; }
        if (!p->status || !p->need) continue;
        p->d[p->have++] = c;
        if (p->have == p->need) { if (p->need == 1) p->d[1] = 0; emit(p, port, cb, ctx); p->have = 0; }   // running status
    }
}
