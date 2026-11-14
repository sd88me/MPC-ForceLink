// Offline test of flsurf's controller -> client filter (build in the armhf container, like flsurf):
//   gcc -o t flsurf_test.c floled.c flcmd.c dfu_guard_fn.c combo.c flswitch.c -lasound -lpthread && ./t
#define main flsurf_main
#include "flsurf.c"
#undef main

static int fails;

static void expect(const char *name, const uint8_t *in, size_t n, const uint8_t *want, size_t wn) {
    upfilt_t f; memset(&f, 0, sizeof f);
    uint8_t out[256];
    size_t o = up_filter(&f, in, n, out);
    int ok = o == wn && !memcmp(out, want, wn);
    printf("%-44s %s\n", name, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

int main(void) {
    // heartbeat reply dropped; pad note passes; button passes
    const uint8_t a[] = {0xF0, 0x47, 0x7F, 0x40, 0x53, 0x00, 0x05, 1, 2, 3, 4, 1, 0xF7, 0x99, 54, 0x7F, 0x90, 0x52, 0x7F};
    const uint8_t aw[] = {0x99, 54, 0x7F, 0x90, 0x52, 0x7F};
    expect("sysex reply dropped, notes pass", a, sizeof a, aw, sizeof aw);
    // knobs: relative steps capped at +1 / -1 (fast turns sent up to 5)
    const uint8_t k[] = {0x90, 0x53, 0x7F, 0xB0, 0x10, 1, 0xB0, 0x10, 5, 0xB0, 0x10, 0x7F, 0xB0, 0x11, 0x7B};
    const uint8_t kw[] = {0x90, 0x53, 0x7F, 0xB0, 0x10, 1, 0xB0, 0x10, 1, 0xB0, 0x10, 0x7F, 0xB0, 0x11, 0x7F};
    expect("knob steps capped at +1 / -1", k, sizeof k, kw, sizeof kw);
    // main encoder CC 100 is not a knob: passes unchanged
    const uint8_t e[] = {0xB0, 0x64, 0x01, 0xB0, 0x64, 0x7F};
    expect("encoder passes", e, sizeof e, e, sizeof e);
    // note layout: top-left pad (54) -> 36 + 56 = 92, bottom-left (110) -> 36, bottom-right (117) -> 43
    note_layout = 1;
    const uint8_t nl[] = {0x99, 54, 0x7F, 0x99, 110, 0x7F, 0x89, 117, 0x00, 0x90, 0x52, 0x7F};
    const uint8_t nlw[] = {0x99, 92, 0x7F, 0x99, 36, 0x7F, 0x89, 43, 0x00, 0x90, 0x52, 0x7F};
    expect("note layout on: pads renumbered, buttons not", nl, sizeof nl, nlw, sizeof nlw);
    // keys layout (base 48): bottom-left 48, next row up +5, top-right 48 + 7 + 35; copies on channel 16
    note_layout = 2; key_base = 48;
    const uint8_t kl[] = {0x99, 110, 0x64, 0x99, 102, 0x7F, 0x89, 61, 0x00};
    const uint8_t klw[] = {0x9F, 110, 0x64, 0x99, 48, 0x64, 0x9F, 102, 0x7F, 0x99, 53, 0x7F, 0x8F, 61, 0x00, 0x89, 90, 0x00};
    expect("keys layout: rows a 4th apart, copies on ch 16", kl, sizeof kl, klw, sizeof klw);
    // lanes (STEP SEQ): lane pads play their note on ch 15 with the press velocity, copied on ch 16; other pads unchanged
    {
        upfilt_t df; memset(&df, 0, sizeof df);
        char cmd[200] = "notemap lanes";
        for (int k = 0; k < 16; k++) sprintf(cmd + strlen(cmd), " %d", 60 + k);
        uint8_t m[220]; size_t mn = 0;
        m[mn++] = 0xF0; m[mn++] = 0x7D; m[mn++] = 0x46;
        memcpy(m + mn, cmd, strlen(cmd)); mn += strlen(cmd); m[mn++] = 0xF7;
        down_msg(NULL, m, mn);
        printf("%-44s %s\n", "notemap lanes sets layout 4", note_layout == 4 && lane_note[15] == 75 ? "ok" : "FAIL");
        if (note_layout != 4) fails++;
    }
    // bottom-left (110) = lane 0, bottom row col 3 (113) = lane 3, row 4 col 0 (86) = lane 12; col 4 (114) and top (54) pass
    const uint8_t ln[] = {0x99, 110, 0x50, 0x99, 113, 0x7F, 0x89, 86, 0x00, 0x99, 114, 0x40, 0x99, 54, 0x40, 0xA9, 110, 0x20};
    const uint8_t lnw[] = {0x9F, 110, 0x50, 0x9E, 60, 0x50, 0x9F, 113, 0x7F, 0x9E, 63, 0x7F, 0x8F, 86, 0x00, 0x8E, 72, 0x00,
                           0x99, 114, 0x40, 0x99, 54, 0x40, 0xAE, 60, 0x20};
    expect("lanes: lane pads -> ch 15 notes + ch 16 copies", ln, sizeof ln, lnw, sizeof lnw);
    note_layout = 0;
    puts(fails ? "FAILURES" : "ALL OK");
    return fails != 0;
}
