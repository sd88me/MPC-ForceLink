// Force Q-link OLED renderer: 128x32 1-bpp framebuffer -> `0B` sysex frames for the controller.
#ifndef FLOLED_H
#define FLOLED_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint8_t px[32][128]; } fl_fb;       // 0 / 1 per pixel
void fl_fb_clear(fl_fb *f);
void fl_fb_text(fl_fb *f, int x, int y, int big, const char *s);   // big: 8x16 bold, else 6x12
void fl_fb_rect(fl_fb *f, int x, int y, int w, int h, int fill);   // fill=0 outline, 1 solid
void fl_fb_text_mpc(fl_fb *f, int y, const char *s);               // MPC OLED font, upper-case, centred
// Q-link layout as the real Force draws it: centred upper-case name + outlined bar 0..127 (bar<0 = none);
// value is accepted for compatibility and not drawn. Same frame as fl_fb_track.
void fl_fb_qlink(fl_fb *f, const char *name, const char *value, int bar);
// Encode as one sysex message for display d (0..7); returns length written (<= cap), 0 if cap too small.
size_t fl_fb_sysex(const fl_fb *f, int d, uint8_t *out, size_t cap);
// Layouts as the Force's own MPC draws them (see FINDINGS 12.1).
void fl_fb_track(fl_fb *f, const char *name, int bar);                 // small caps name, outlined level bar
void fl_fb_header(fl_fb *f, const char *label, const char *big);        // disp 16: white label bar + large text
// Sysex for a page/column sub-region (partial update, like MPC's level-bar redraws).
size_t fl_fb_sysex_region(const fl_fb *f, int d, int y0p, int y1p, int x0, int x1, uint8_t *out, size_t cap);
#endif
