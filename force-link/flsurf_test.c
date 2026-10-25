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
    note_layout = 0;
    puts(fails ? "FAILURES" : "ALL OK");
    return fails != 0;
}
