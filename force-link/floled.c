#include "floled.h"
#include "flfont.h"
#include "flfont_mpc.h"
#include "flfont_big.h"
#include <string.h>

void fl_fb_clear(fl_fb *f) { memset(f, 0, sizeof *f); }

void fl_fb_text(fl_fb *f, int x, int y, int big, const char *s) {
    int w = big ? F8X16_W : F6X12_W, h = big ? F8X16_H : F6X12_H;
    for (; *s && x + w <= 128; s++, x += w) {
        int c = (unsigned char)*s; if (c < 32 || c > 126) c = '?';
        for (int r = 0; r < h; r++) {
            uint8_t row = big ? f8x16[c - 32][r] : f6x12[c - 32][r];
            for (int b = 0; b < w; b++) if (row & (0x80 >> b)) { int yy = y + r; if (yy >= 0 && yy < 32) f->px[yy][x + b] = 1; }
        }
    }
}

void fl_fb_rect(fl_fb *f, int x, int y, int w, int h, int fill) {
    for (int j = y; j < y + h && j < 32; j++)
        for (int i = x; i < x + w && i < 128; i++)
            if (fill || j == y || j == y + h - 1 || i == x || i == x + w - 1) f->px[j][i] = 1;
}

// MPC's OLED font: advance = glyph width + 1; text is upper-cased and centred as floor((128 - advances) / 2)
static int mpc_glyph(int c) { if (c >= 'a' && c <= 'z') c -= 32; return (c < 32 || c > 126) ? '?' - 32 : c - 32; }

static int mpc_text_width(const char *s) {
    int w = 0; for (; *s; s++) w += fmpc[mpc_glyph((unsigned char)*s)][8] + 1;
    return w;
}

void fl_fb_text_mpc(fl_fb *f, int y, const char *s) {
    int w = mpc_text_width(s), x = w >= 128 ? 0 : (128 - w) / 2;
    for (; *s && x < 128; s++) {
        const unsigned char *g = fmpc[mpc_glyph((unsigned char)*s)];
        for (int r = 0; r < FMPC_H; r++)
            for (int b = 0; b < g[8]; b++)
                if (g[r] & (0x80 >> b) && x + b < 128 && y + r >= 0 && y + r < 32) f->px[y + r][x + b] = 1;
        x += g[8] + 1;
    }
}

// Q-link frame as a real Force draws it (captured, FINDINGS 20): same frame as fl_fb_track; the value text
// is not shown on the OLED.
void fl_fb_qlink(fl_fb *f, const char *name, const char *value, int bar) {
    (void)value;
    fl_fb_track(f, name, bar);
}

// Large value font (FINDINGS 24): 12 rows; glyphs harvested from a capture (flfont_big.h), others from the small
// font scaled 2x wide and 7 -> 12 rows. Spacing fitted to every glyph pair in the capture:
// gap = 1 + right bearing(a) + left bearing(b); a space adds 5. Centred on the cells: T = lb(first) + ink +
// rb(last) + 1, x = floor((128 - T) / 2) + lb(first).
static int big_width(int c) {
    if (c == ' ') return 0;
    if (c < 32 || c > 126) c = '?';
    if (fbig[c - 32][0]) return fbig[c - 32][0];
    return fmpc[mpc_glyph(c)][8] * 2;
}

static int big_lb(int c) { return c >= '0' && c <= '9' && c != '1' ? 0 : 1; }
static int big_rb(int c) { return c == '1' ? 2 : c == '.' ? 1 : (c == '-' || c == 'R') ? -1 : 0; }

// pixels from the end of glyph a to the start of the next visible glyph (spaces folded in)
static int big_gap(const char *p) {
    int sp = 0; const char *q = p + 1;
    while (*q == ' ') { sp += 5; q++; }
    if (!*q) return 0;
    int lb = (*q == '+' || *q == '-') ? 0 : big_lb((unsigned char)*q);   // only for the first glyph's centring
    return 1 + big_rb((unsigned char)*p) + sp + lb;
}

static int big_ink_width(const char *s) {
    int w = 0;
    while (*s == ' ') s++;
    for (const char *p = s; *p; p++) {
        if (*p == ' ') continue;
        w += big_width((unsigned char)*p) + big_gap(p);
    }
    return w;
}

void fl_fb_text_big(fl_fb *f, int y, const char *s) {
    while (*s == ' ') s++;
    if (!*s) return;
    const char *last = s + strlen(s) - 1;
    while (last > s && *last == ' ') last--;
    int lb = big_lb((unsigned char)*s), t = lb + big_ink_width(s) + big_rb((unsigned char)*last) + 1;
    int x = (t >= 128 ? 0 : (128 - t) / 2) + lb;
    for (const char *p = s; *p && x < 128; p++) {
        int c = (unsigned char)*p;
        if (c == ' ') continue;
        if (c < 32 || c > 126) c = '?';
        int gw = big_width(c);
        for (int r = 0; r < FBIG_H; r++)
            for (int b = 0; b < gw; b++) {
                int on;
                if (fbig[c - 32][0]) on = (fbig[c - 32][1 + r] >> (15 - b)) & 1;
                else on = (fmpc[mpc_glyph(c)][1 + r * 7 / FBIG_H] >> (7 - b / 2)) & 1;
                if (on && x + b < 128 && y + r >= 0 && y + r < 32) f->px[y + r][x + b] = 1;
            }
        x += gw + big_gap(p);
    }
}

// Fill of a Q-link bar between x 3 and 124 (122 px). bar 0-127. Bipolar (pan-like): from the centre x 64 to the
// value, at least the centre pixel (as captured: "C" = x 64 only, full L = x 3..64, full R = x 64..124).
static void bar_fill(fl_fb *f, int y, int h, int bar, int bipolar) {
    if (bar < 0) return;
    if (bar > 127) bar = 127;
    if (!bipolar) {
        if (bar) fl_fb_rect(f, 3, y, (122 * bar + 63) / 127, h, 1);
        return;
    }
    int x0 = 64, x1 = 64;
    if (bar > 64) x1 = 64 + ((bar - 64) * 60 + 31) / 63;
    else if (bar < 64) x0 = 64 - ((64 - bar) * 61 + 32) / 64;
    fl_fb_rect(f, x0, y, x1 - x0 + 1, h, 1);
}

// Name view (idle): name on rows 7-13, outline rows 18-29, fill rows 21-26.
void fl_fb_qlink_name(fl_fb *f, const char *name, int bar, int bipolar) {
    fl_fb_clear(f);
    char n[32]; strncpy(n, name, sizeof n - 1); n[sizeof n - 1] = 0;
    fl_fb_text_mpc(f, 6, n);
    if (bar >= 0) {
        fl_fb_rect(f, 0, 18, 128, 12, 0);
        bar_fill(f, 21, 6, bar, bipolar);
    }
}

// Value view (knob touched or turned): large value on rows 5-16, outline rows 24-31, fill rows 27-28.
void fl_fb_qlink_value(fl_fb *f, const char *value, int bar, int bipolar) {
    fl_fb_clear(f);
    char v[24]; strncpy(v, value, sizeof v - 1); v[sizeof v - 1] = 0;
    fl_fb_text_big(f, 5, v);
    fl_fb_rect(f, 0, 24, 128, 8, 0);
    bar_fill(f, 27, 2, bar, bipolar);
}

size_t fl_fb_sysex_region(const fl_fb *f, int d, int y0p, int y1p, int x0, int x1, uint8_t *out, size_t cap) {
    if (y0p < 0 || y1p > 3 || y0p > y1p || x0 < 0 || x1 > 127 || x0 > x1) return 0;
    int pages = y1p - y0p + 1, w = x1 - x0 + 1; uint8_t cols[512]; size_t nb = (size_t)pages * w;
    for (int pg = 0; pg < pages; pg++)
        for (int x = 0; x < w; x++) {
            uint8_t v = 0; for (int b = 0; b < 8; b++) v |= f->px[(y0p + pg) * 8 + b][x0 + x] << b;
            cols[pg * w + x] = v;
        }
    size_t packed = (nb * 8 + 6) / 7, total = 12 + packed + 1;
    if (cap < total) return 0;
    out[0] = 0xF0; out[1] = 0x47; out[2] = 0x7F; out[3] = 0x40; out[4] = 0x0B;
    size_t n = 5 + packed; out[5] = n >> 7; out[6] = n & 127;
    out[7] = d; out[8] = y0p; out[9] = y1p; out[10] = x0; out[11] = x1;
    memset(out + 12, 0, packed);
    for (size_t bit = 0; bit < nb * 8; bit++) {          // MSB-first bit stream into 7-bit bytes
        int v = (cols[bit >> 3] >> (7 - (bit & 7))) & 1;
        if (v) out[12 + bit / 7] |= 1 << (6 - bit % 7);
    }
    out[12 + packed] = 0xF7; return total;
}

size_t fl_fb_sysex(const fl_fb *f, int d, uint8_t *out, size_t cap) { return fl_fb_sysex_region(f, d, 0, 3, 0, 127, out, cap); }

void fl_fb_track(fl_fb *f, const char *name, int bar) {
    fl_fb_clear(f);
    char n[32]; strncpy(n, name, sizeof n - 1); n[sizeof n - 1] = 0;
    fl_fb_text_mpc(f, 6, n);                               // glyph row 0 is blank: ink on rows 7-13
    if (bar >= 0) {                                        // outline rows 18-29, fill rows 21-26 from x 3, 122 px max
        if (bar > 127) bar = 127;
        fl_fb_rect(f, 0, 18, 128, 12, 0);
        if (bar) fl_fb_rect(f, 3, 21, (122 * bar + 63) / 127, 6, 1);
    }
}

void fl_fb_header(fl_fb *f, const char *label, const char *big) {
    fl_fb_clear(f);
    fl_fb_rect(f, 0, 0, 128, 14, 1);                      // white label bar, dark text knocked out of it
    static fl_fb t; fl_fb_clear(&t);
    char l[22]; strncpy(l, label, 21); l[21] = 0;
    fl_fb_text(&t, (128 - (int)strlen(l) * F6X12_W) / 2, 1, 0, l);
    for (int y = 0; y < 14; y++) for (int x = 0; x < 128; x++) f->px[y][x] ^= t.px[y][x];
    char b[17]; strncpy(b, big, 16); b[16] = 0;
    fl_fb_text(f, (128 - (int)strlen(b) * F8X16_W) / 2, 15, 1, b);
}
