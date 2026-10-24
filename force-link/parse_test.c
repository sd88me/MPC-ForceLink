#include "flparse.h"
#include <stdio.h>
#include <string.h>
static int fails;
static char last[10][128]; static int nl;
static void cb(void *c, const char *l) { (void)c; if (nl < 10) strcpy(last[nl++], l); }
static void t(int port, const char *hex_unused, const uint8_t *b, size_t n, const char *want, int idx) {
    (void)hex_unused; fl_parser p; fl_parser_init(&p); nl = 0; fl_parser_feed(&p, port, b, n, cb, 0);
    int ok = nl > idx && !strcmp(last[idx], want);
    printf("%s  got [%s] want [%s]\n", ok ? "ok  " : "FAIL", nl > idx ? last[idx] : "(none)", want); if (!ok) fails++;
}
int main(void) {
    uint8_t pad[] = {0x99, 54, 100}, padbr[] = {0x99, 117, 127}, padup[] = {0x89, 54, 0}, padon0[] = {0x99, 54, 0};
    uint8_t btn[] = {0x90, 4, 127}, launch[] = {0x90, 116, 127}, touch[] = {0x90, 0x55, 127};
    uint8_t kcw[] = {0x90, 0x52, 127, 0xB0, 0x12, 1, 0xB0, 0x12, 2, 0xB0, 0x12, 3};
    uint8_t kccw[] = {0xB0, 0x10, 2, 0xB0, 0x10, 1, 0xB0, 0x10, 0, 0xB0, 0x10, 127};
    uint8_t run[] = {0x99, 55, 5, 56, 6}, sx[] = {0xF0, 0x47, 0x7F, 1, 2, 0xF7}, rt[] = {0x99, 0xF8, 60, 0xFE, 99};
    t(0, "", pad, 3, "PAD n=54 row=0 col=0 down vel=100", 0);
    t(0, "", padbr, 3, "PAD n=117 row=7 col=7 down vel=127", 0);
    t(0, "", padup, 3, "PAD n=54 row=0 col=0 up vel=0", 0);
    t(0, "", padon0, 3, "PAD n=54 row=0 col=0 up vel=0", 0);
    t(0, "", btn, 3, "BTN NOTE down vel=127", 0);
    t(0, "", launch, 3, "BTN LAUNCH down vel=127", 0);
    t(0, "", touch, 3, "TOUCH knob=3 down", 0);
    t(0, "", kcw, 12, "KNOB k=3 delta=1", 1);
    t(0, "", kcw, 12, "KNOB k=3 delta=1", 3);
    t(1, "", kccw, 12, "KNOB k=1 delta=-1", 1);
    t(1, "", kccw, 12, "KNOB k=1 delta=-1", 3);
    t(0, "", run, 5, "PAD n=56 row=0 col=2 down vel=6", 1);
    t(1, "", sx, 6, "SYSEX port=1 len=6", 0);
    t(0, "", rt, 5, "PAD n=60 row=0 col=6 down vel=99", 0);
    uint8_t enc[] = {0xB0, 100, 127};
    t(0, "", enc, 3, "ENCODER delta=-1", 0);
    printf("%s\n", fails ? "FAILED" : "all ok"); return fails != 0;
}
