// Offline check: render sample Q-link frames, write a PBM montage + verify the sysex round-trips (decoded like the captured frames).
#include "floled.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    const char *nm[8] = {"Cutoff", "Resonance", "Attack", "Decay", "Sustain", "Release", "LFO Rate", "A VERY LONG NAME XYZ"};
    const char *vl[8] = {"1.2kHz", "35%", "0.5ms", "120ms", "-6.0dB", "80ms", "4.2Hz", ""};
    int bars[8] = {90, 45, 5, 60, 100, 70, 30, -1};
    static fl_fb fb[8]; int bad = 0;
    for (int i = 0; i < 8; i++) {
        fl_fb_qlink(&fb[i], nm[i], vl[i], bars[i]);
        uint8_t sx[700]; size_t n = fl_fb_sysex(&fb[i], i, sx, sizeof sx);
        // decode independently: unpack 7-bit stream -> columns -> compare
        int len = (sx[5] << 7) | sx[6]; if (n != (size_t)len + 8 || sx[0] != 0xF0 || sx[n - 1] != 0xF7 || sx[7] != i) bad++;
        uint8_t cols[512]; memset(cols, 0, sizeof cols);
        for (size_t bit = 0; bit < 512 * 8; bit++) {
            int v = (sx[12 + bit / 7] >> (6 - bit % 7)) & 1; if (v) cols[bit >> 3] |= 1 << (7 - (bit & 7));
        }
        for (int y = 0; y < 32; y++) for (int x = 0; x < 128; x++) if (((cols[(y / 8) * 128 + x] >> (y % 8)) & 1) != fb[i].px[y][x]) bad++;
    }
    FILE *o = fopen("oled_montage.pbm", "w");           // 2 columns x 4 rows, 2px gap
    int W = 2 * 130, H = 4 * 34; fprintf(o, "P1\n%d %d\n", W, H);
    for (int y = 0; y < H; y++) { for (int x = 0; x < W; x++) {
        int c = x / 130, r = y / 34, px = x % 130, py = y % 34, v = 0;
        if (px < 128 && py < 32) { v = fb[r * 2 + c].px[py][px]; } fputc(v ? '1' : '0', o); } fputc('\n', o); }
    fclose(o); printf(bad ? "FAIL %d\n" : "roundtrip ok\n", bad); return bad != 0;
}
