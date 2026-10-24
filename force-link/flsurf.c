// flsurf: the Force controller as a MIDI surface for version B (a controller script on the Live III), over TCP.
// Based on forcelink: owns the controller (Force MPC stopped), replays the start-up handshake, keeps the heartbeat.
// One TCP client (port 6770), raw MIDI both ways:
//   controller -> client: as sent, except heartbeat/identity replies (dropped); pads renumbered when "notemap on".
//   client -> controller: sysex to the Public port, other messages to Private, paced; F0 7D 46 <text> F7 runs a
//     flcmd text command (e.g. "qlink 1 Cutoff|1.2kHz|90" draws Q-link OLED 1). dfu_guard() filters as in flnet.
//   flsurf [-r] [-p port] handshake.syx
#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "floled.h"
#include "flcmd.h"

typedef struct { uint8_t head[5]; int n; int drop; } dfu_guard_t;   // dfu_guard_fn.c (generated from flnet.c)
size_t dfu_guard(dfu_guard_t *g, const uint8_t *in, size_t n, uint8_t *out);

static int find_card(void) {          // the controller's ALSA card, by its id "Force"
    FILE *f = fopen("/proc/asound/cards", "r"); char l[256], id[32]; int n = -1, c;
    if (!f) return -1;
    while (fgets(l, sizeof l, f)) if (sscanf(l, " %d [%31[^] ]", &c, id) == 2 && !strcmp(id, "Force")) n = c;
    fclose(f); return n;
}

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }
static int raw;

static int send_all(snd_rawmidi_t *o, const uint8_t *b, size_t n) {
    while (n) {                       // rawmidi write may be partial; pace large sysex bursts
        ssize_t w = snd_rawmidi_write(o, b, n > 256 ? 256 : n);
        if (w == -EAGAIN) { usleep(2000); continue; }
        if (w < 0) return (int)w;
        snd_rawmidi_drain(o); b += w; n -= w;
    }
    return 0;
}

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); rewind(f);
    uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != *n) { fclose(f); free(b); return NULL; }
    fclose(f); return b;
}

// split a buffer of concatenated sysex and send each with a short gap (the controller is not a fan of bursts)
static void send_sysex_file(snd_rawmidi_t *o, const uint8_t *b, size_t n) {
    size_t s = 0;
    for (size_t i = 0; i < n; i++) if (b[i] == 0xF7) { send_all(o, b + s, i - s + 1); usleep(3000); s = i + 1; }
}

typedef struct { snd_rawmidi_t *pub, *priv; } io_t;
static void send_cb(void *ctx, int cable, const uint8_t *b, size_t n) {
    io_t *io = ctx; send_all(cable ? io->priv : io->pub, b, n);
}

// "notemap on|off" from the script: pads become plain notes for MPC tracks, 36 + 8 * row-from-bottom + col (36 = the
// bottom-left pad, the usual drum layout start)
static int note_layout;

// controller -> client. Sysex from the controller is only replies (heartbeat, identity, config echoes): dropped.
typedef struct { int in_sysex; uint8_t st, m[2]; int mn; } upfilt_t;

static size_t up_filter(upfilt_t *f, const uint8_t *b, size_t n, uint8_t *out) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = b[i];
        if (c == 0xF0) { f->in_sysex = 1; continue; }
        if (c == 0xF7) { f->in_sysex = 0; continue; }
        if (f->in_sysex) continue;
        if (c & 0x80) { f->st = c; f->mn = 0; continue; }
        if (!f->st) continue;
        f->m[f->mn++] = c;
        int need = (f->st & 0xF0) == 0xC0 || (f->st & 0xF0) == 0xD0 ? 1 : 2;
        if (f->mn < need) continue;
        f->mn = 0;
        // Q-link knobs (CC 0x10-0x17) already arrive as relative steps after flsurf's handshake (1, 2 ... up,
        // 127, 126 ... down; seen on the device 2026-10-24), which is MPC's infinite-CC format: passed unchanged.
        uint8_t d0 = f->m[0];
        if (note_layout && (f->st == 0x99 || f->st == 0x89 || f->st == 0xA9) && d0 >= 54 && d0 <= 117) {
            int i = d0 - 54;
            d0 = (uint8_t)(36 + (7 - i / 8) * 8 + i % 8);
        }
        out[o++] = f->st; out[o++] = d0;
        if (need == 2) out[o++] = f->m[1];
    }
    return o;
}

// client -> controller, one complete message at a time (after dfu_guard)
typedef struct { uint8_t m[2048]; size_t n; int sysex; } downsplit_t;

static void down_msg(io_t *io, const uint8_t *m, size_t n) {
    if (n >= 4 && m[0] == 0xF0 && m[1] == 0x7D && m[2] == 0x46) {     // text command for flcmd
        char line[600];
        size_t l = n - 4 < sizeof line - 1 ? n - 4 : sizeof line - 1;
        memcpy(line, m + 3, l); line[l] = 0;
        if (!strncmp(line, "notemap ", 8)) { note_layout = !strcmp(line + 8, "on"); return; }
        fl_cmd(line, send_cb, io);
        return;
    }
    if (m[0] != 0xF0 && (m[0] & 0xF0) <= 0xA0)
        return;                        // notes from MPC (its pad feedback) would be taken as LED commands: drop
    send_all(m[0] == 0xF0 ? io->pub : io->priv, m, n);
    if (m[0] == 0xF0) usleep(3000);
}

static void down_feed(downsplit_t *d, io_t *io, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = b[i];
        if (c == 0xF0) { d->sysex = 1; d->n = 0; }
        if (d->sysex) {
            if (d->n < sizeof d->m) d->m[d->n++] = c;
            if (c == 0xF7) { down_msg(io, d->m, d->n); d->sysex = 0; d->n = 0; }
            continue;
        }
        if (c & 0x80) { d->m[0] = c; d->n = 1; continue; }
        if (!d->n) continue;
        d->m[d->n++] = c;
        int need = (d->m[0] & 0xF0) == 0xC0 || (d->m[0] & 0xF0) == 0xD0 ? 2 : 3;
        if ((int)d->n == need) { down_msg(io, d->m, d->n); d->n = 1; }   // keep running status
    }
}

int main(int argc, char **argv) {
    int a = 1, port = 6770;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (!strcmp(argv[a], "-r")) raw = 1;
        else if (!strcmp(argv[a], "-p") && a + 1 < argc) port = atoi(argv[++a]);
        else { fprintf(stderr, "usage: %s [-r] [-p port] handshake.syx\n", argv[0]); return 2; }
    }
    if (a >= argc) { fprintf(stderr, "need handshake.syx\n"); return 2; }
    int card = find_card();
    if (card < 0) { fprintf(stderr, "no Force controller card\n"); return 1; }
    char pub[32], priv[32];
    snprintf(pub, sizeof pub, "hw:%d,0,0", card); snprintf(priv, sizeof priv, "hw:%d,0,1", card);
    snd_rawmidi_t *in[2] = {0}, *outp = NULL, *outc = NULL;
    int e;
    if ((e = snd_rawmidi_open(&in[0], &outp, pub, SND_RAWMIDI_NONBLOCK)) < 0 ||
        (e = snd_rawmidi_open(&in[1], &outc, priv, SND_RAWMIDI_NONBLOCK)) < 0) {
        fprintf(stderr, "open controller: %s (is MPC stopped?)\n", snd_strerror(e)); return 1;
    }
    // writes block (paced), reads must not: the flag is shared by the handles of one open, so set both explicitly
    snd_rawmidi_nonblock(outp, 0); snd_rawmidi_nonblock(outc, 0);
    snd_rawmidi_nonblock(in[0], 1); snd_rawmidi_nonblock(in[1], 1);
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGPIPE, SIG_IGN);
    size_t n; uint8_t *hs = slurp(argv[a], &n); if (!hs) return 1;
    send_sysex_file(outp, hs, n); free(hs);
    fprintf(stderr, "flsurf: controller card %d, handshake sent, port %d\n", card, port);

    int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1, cl = -1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(ls, (void *)&sa, sizeof sa) || listen(ls, 1)) { perror("bind"); return 1; }

    io_t io = {outp, outc};
    upfilt_t uf[2]; memset(uf, 0, sizeof uf);
    downsplit_t ds; memset(&ds, 0, sizeof ds);
    dfu_guard_t g; memset(&g, 0, sizeof g);
    time_t last_ping = 0;
    while (!stop) {
        if (time(NULL) - last_ping >= 4) {
            static const uint8_t ping[] = {0xF0, 0x47, 0x00, 0x40, 0x52, 0x00, 0x04, 0x31, 0x39, 0x16, 0x40, 0xF7};
            send_all(outp, ping, sizeof ping); last_ping = time(NULL);
        }
        struct pollfd pfd[8]; int np = 0;
        np += snd_rawmidi_poll_descriptors(in[0], pfd + np, 2);
        int c0 = np;
        np += snd_rawmidi_poll_descriptors(in[1], pfd + np, 2);
        int c1 = np;
        pfd[np++] = (struct pollfd){ls, POLLIN, 0};
        if (cl >= 0) pfd[np++] = (struct pollfd){cl, POLLIN, 0};
        poll(pfd, np, 200);
        for (int i = 0; i < 2; i++) {
            uint8_t b[256], o[400]; ssize_t r;
            while ((r = snd_rawmidi_read(in[i], b, sizeof b)) > 0) {
                if (raw) { fprintf(stderr, "ctl%d:", i); for (ssize_t k = 0; k < r; k++) fprintf(stderr, " %02X", b[k]); fputc('\n', stderr); }
                size_t on = up_filter(&uf[i], b, (size_t)r, o);
                if (on && cl >= 0 && write(cl, o, on) < 0) { close(cl); cl = -1; fprintf(stderr, "client gone\n"); }
            }
        }
        if (pfd[c1].revents & POLLIN) {
            int c = accept(ls, NULL, NULL);
            if (c >= 0) {
                setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                if (cl >= 0) close(cl);
                cl = c; memset(&ds, 0, sizeof ds); memset(&g, 0, sizeof g);
                fprintf(stderr, "client connected\n");
            }
        }
        if (cl >= 0 && np > c1 + 1 && (pfd[c1 + 1].revents & (POLLIN | POLLHUP | POLLERR))) {
            uint8_t b[1024], gb[1024 + 8];
            ssize_t r = read(cl, b, sizeof b);
            if (r <= 0) { close(cl); cl = -1; fprintf(stderr, "client gone\n"); continue; }
            if (raw) { fprintf(stderr, "client %zd:", r); for (ssize_t k = 0; k < r && k < 24; k++) fprintf(stderr, " %02X", b[k]); fputc('\n', stderr); }
            down_feed(&ds, &io, gb, dfu_guard(&g, b, (size_t)r, gb));
        }
        (void)c0;
    }
    if (cl >= 0) close(cl);
    close(ls);
    snd_rawmidi_close(in[0]); snd_rawmidi_close(in[1]); snd_rawmidi_close(outp); snd_rawmidi_close(outc);
    return 0;
}
