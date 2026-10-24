// Offline preview: read daemon commands (stdin), decode the controller messages they produce exactly as the
// hardware would (pad RGB `65`, OLED `0B` incl. partial regions) and write a PPM of the Force surface.
//   ./preview < commands.txt > out.ppm      (idx row 0 drawn at the bottom, KikGen numbering)
#include "flcmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t pads[64][3]; static uint8_t oled[17][32][128]; static int has_oled[17]; static uint8_t btn[128];

static void rx(void *ctx, int cable, const uint8_t *b, size_t n) {
    (void)ctx;
    if (cable == 1 && n == 3 && b[0] == 0xB0) { btn[b[1]] = b[2]; return; }
    if (n >= 12 && b[1] == 0x47 && b[3] == 0x40 && b[4] == 0x65) { if (b[7] < 64) memcpy(pads[b[7]], b + 8, 3); return; }
    if (n >= 13 && b[4] == 0x0B) {
        int d = b[7], y0 = b[8], y1 = b[9], x0 = b[10], x1 = b[11]; if (d > 16) return; has_oled[d] = 1;
        int pages = y1 - y0 + 1, w = x1 - x0 + 1; size_t nb = (size_t)pages * w; uint8_t cols[512] = {0};
        for (size_t bit = 0; bit < nb * 8; bit++) if ((b[12 + bit / 7] >> (6 - bit % 7)) & 1) cols[bit >> 3] |= 1 << (7 - (bit & 7));
        for (int pg = 0; pg < pages; pg++) for (int x = 0; x < w; x++) for (int bt = 0; bt < 8; bt++)
            oled[d][(y0 + pg) * 8 + bt][x0 + x] = (cols[pg * w + x] >> bt) & 1;
    }
}

int main(void) {
    char line[512];
    while (fgets(line, sizeof line, stdin)) { line[strcspn(line, "\n")] = 0; if (*line && *line != '#') if (fl_cmd(line, rx, NULL) < 0) fprintf(stderr, "bad command: %s\n", line); }
    const int CELL = 36, W = 8 * 132, H = 8 * CELL + 6 + 2 * 36;
    static uint8_t img[1200 * 400 * 3]; if (W * H * 3 > (int)sizeof img) return 1; memset(img, 24, (size_t)W * H * 3);
    for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) {
        uint8_t *p = pads[r * 8 + c]; int y0 = (7 - r) * CELL, x0 = c * CELL + 2;
        for (int y = 2; y < CELL - 2; y++) for (int x = 2; x < CELL - 2; x++) { uint8_t *o = img + ((size_t)(y0 + y) * W + x0 + x) * 3; for (int k = 0; k < 3; k++) o[k] = (uint8_t)(p[k] * 2); }
    }
    for (int d = 0; d < 9; d++) {                         // 8 Q-link displays + display 16
        int dd = d < 8 ? d : 16; int ox = (d % 8) * 132, oy = 8 * CELL + 6 + (d < 8 ? 0 : 36);
        if (d == 8) ox = 0;
        for (int y = 0; y < 32; y++) for (int x = 0; x < 128; x++) { uint8_t v = oled[dd][y][x] ? 235 : 10; uint8_t *o = img + ((size_t)(oy + y) * W + ox + x) * 3; o[0] = o[1] = o[2] = v; }
    }
    printf("P6\n%d %d\n255\n", W, H); fwrite(img, 3, (size_t)W * H, stdout); return 0;
}
