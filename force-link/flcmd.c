#include "flcmd.h"
#include "floled.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fl_dim(const uint8_t in[3], uint8_t out[3]) { for (int i = 0; i < 3; i++) out[i] = (uint8_t)(in[i] * 5 / 100); }

static void pad(fl_send_fn s, void *c, unsigned n, unsigned r, unsigned g, unsigned b) {
    uint8_t m[] = {0xF0, 0x47, 0x7F, 0x40, 0x65, 0, 4, n & 127, r & 127, g & 127, b & 127, 0xF7}; s(c, 0, m, sizeof m);
}

int fl_cmd(const char *line, fl_send_fn send, void *ctx) {
    unsigned a, b, c, d, e, f; char name[64];
    static fl_fb fb; static uint8_t sx[700];
    if (sscanf(line, "pad %u %u %u %u", &a, &b, &c, &d) == 4) { pad(send, ctx, a, b, c, d); return 0; }
    if (sscanf(line, "clip %u %u %15s %u %u %u", &a, &b, name, &c, &d, &e) == 6 && a >= 1 && a <= 8 && b >= 1 && b <= 7) {
        uint8_t col[3] = {c, d, e}, dim[3]; fl_dim(col, dim);
        int empty = !strcmp(name, "empty"); pad(send, ctx, b * 8 + (a - 1), empty ? dim[0] : col[0], empty ? dim[1] : col[1], empty ? dim[2] : col[2]); return 0;
    }
    if (sscanf(line, "hdrrow %u", &a) == 1 && a >= 1 && a <= 8) { pad(send, ctx, a - 1, 0x19, 0x19, 0x19); return 0; }
    if (sscanf(line, "btn %u %u", &a, &b) == 2) { uint8_t m[] = {0xB0, a & 127, b & 127}; send(ctx, 1, m, sizeof m); return 0; }
    if (!strncmp(line, "syx ", 4)) {
        uint8_t m[512]; size_t k = 0; char tmp[1024]; strncpy(tmp, line + 4, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
        for (char *t = strtok(tmp, " "); t && k < sizeof m; t = strtok(NULL, " ")) m[k++] = (uint8_t)strtoul(t, NULL, 16);
        send(ctx, 0, m, k); return 0;
    }
    if (!strcmp(line, "quit")) return 1;
    // text commands: <cmd> <n> <rest with | separators>
    char cmd[8]; int n; char rest[200];
    if (sscanf(line, "%7s %d %199[^\n]", cmd, &n, rest) == 3 && n >= 1 && n <= 8) {
        char *p2 = strchr(rest, '|');
        if (!strcmp(cmd, "qlink") && p2) {
            *p2++ = 0; char *p3 = strchr(p2, '|'); int bar = -1; if (p3) { *p3++ = 0; bar = atoi(p3); }
            fl_fb_qlink(&fb, rest, p2, bar); size_t l = fl_fb_sysex(&fb, n - 1, sx, sizeof sx); send(ctx, 0, sx, l); return 0;
        }
        if (!strcmp(cmd, "track") && p2) {
            *p2++ = 0; fl_fb_track(&fb, rest, atoi(p2)); size_t l = fl_fb_sysex(&fb, n - 1, sx, sizeof sx); send(ctx, 0, sx, l); return 0;
        }
        if (!strcmp(cmd, "bar")) {          // "bar <n> <name> <bar>": name is needed to redraw the same frame, only pages 2-3 sent
            char *sp = strrchr(rest, ' '); if (!sp) return -1; *sp++ = 0;
            fl_fb_track(&fb, rest, atoi(sp)); size_t l = fl_fb_sysex_region(&fb, n - 1, 2, 3, 0, 127, sx, sizeof sx); send(ctx, 0, sx, l); return 0;
        }
    }
    if (!strncmp(line, "hdr ", 4)) {
        strncpy(name, line + 4, sizeof name - 1); name[sizeof name - 1] = 0; char *p2 = strchr(name, '|'); if (!p2) return -1; *p2++ = 0;
        fl_fb_header(&fb, name, p2); size_t l = fl_fb_sysex(&fb, 16, sx, sizeof sx); send(ctx, 0, sx, l); return 0;
    }
    (void)f; return -1;
}
