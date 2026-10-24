#include "flcmd.h"
#include "floled.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s\n", msg); fails++; } else printf("ok   %s\n", msg); } while (0)
static uint8_t last[700]; static size_t lastn; static int lastcable, nmsg;
static void cap(void *c, int cable, const uint8_t *b, size_t n) { (void)c; memcpy(last, b, n); lastn = n; lastcable = cable; nmsg++; }
int main(void) {
    // dim colour rule vs the 7 captured (full, empty) pairs
    static const uint8_t t[7][2][3] = {
        {{0x7f,0x19,0x64},{0x06,0x01,0x05}}, {{0x19,0x7c,0x7f},{0x01,0x06,0x06}}, {{0x7f,0x34,0x19},{0x06,0x02,0x01}},
        {{0x60,0x19,0x7f},{0x04,0x01,0x06}}, {{0x19,0x27,0x7f},{0x01,0x01,0x06}}, {{0x20,0x7f,0x19},{0x01,0x06,0x01}}, {{0x7f,0x19,0x49},{0x06,0x01,0x03}}};
    int okd = 1; for (int i = 0; i < 7; i++) { uint8_t o[3]; fl_dim(t[i][0], o); if (memcmp(o, t[i][1], 3)) { okd = 0; printf("dim %d: %02x%02x%02x\n", i, o[0], o[1], o[2]); } }
    CHECK(okd, "fl_dim reproduces all 7 captured empty-slot colours");
    // clip command -> pad sysex at row*8+col
    nmsg = 0; fl_cmd("clip 3 2 clip 127 52 25", cap, NULL);
    CHECK(nmsg == 1 && lastcable == 0 && last[4] == 0x65 && last[7] == 2 * 8 + 2 && last[8] == 127 && last[9] == 52 && last[10] == 25, "clip -> pad 18 full colour");
    fl_cmd("clip 3 2 empty 127 52 25", cap, NULL);
    CHECK(last[8] == 6 && last[9] == 2 && last[10] == 1, "clip empty -> dim");
    fl_cmd("btn 82 3", cap, NULL); CHECK(lastcable == 1 && lastn == 3 && last[0] == 0xB0 && last[1] == 82 && last[2] == 3, "btn -> CC on cable 1");
    CHECK(fl_cmd("quit", cap, NULL) == 1 && fl_cmd("bogus 1 2", cap, NULL) == -1, "quit / unknown");
    // partial region update equals the same bytes cut out of a full frame
    static fl_fb f; fl_fb_track(&f, "Drum Loop", 60);
    uint8_t reg[700]; size_t rn = fl_fb_sysex_region(&f, 0, 2, 3, 71, 88, reg, sizeof reg);
    CHECK(rn == 12 + (2 * 18 * 8 + 6) / 7 + 1 && reg[7] == 0 && reg[8] == 2 && reg[9] == 3 && reg[10] == 71 && reg[11] == 88 && (reg[5] << 7 | reg[6]) == 47, "region sysex length matches the captured 47-byte payload form");
    CHECK(fl_fb_sysex_region(&f, 0, 0, 4, 0, 127, reg, sizeof reg) == 0, "bad region rejected");
    nmsg = 0; fl_cmd("track 2 Top Loop|90", cap, NULL); CHECK(nmsg == 1 && last[7] == 1 && lastn == 12 + 586 + 1, "track cmd -> full frame disp 1");
    fl_cmd("hdr TRACK|Drum Loop", cap, NULL); CHECK(last[7] == 16, "hdr cmd -> display 16");
    fl_cmd("bar 4 Pads 100", cap, NULL); CHECK(last[7] == 3 && last[8] == 2 && last[9] == 3, "bar cmd -> pages 2-3 only");
    printf(fails ? "FAILED\n" : "all ok\n"); return fails != 0;
}
