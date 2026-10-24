// forcelink: replace MPC as the owner of the Force's controller (pads, buttons, knobs, OLEDs).
// Run with MPC stopped (systemctl stop acvs). Events go to stdout, one per line; commands on stdin.
//   forcelink [-r] [-p pattern.syx] handshake.syx
//   stdin commands: see flcmd.h (pad, clip, hdrrow, btn, qlink, track, bar, hdr, syx, quit)
#include <alsa/asoundlib.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "flparse.h"
#include "floled.h"
#include "flcmd.h"

#define PORT_PUBLIC  "hw:1,0,0"
#define PORT_PRIVATE "hw:1,0,1"

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }
static int raw;

static void out_line(void *ctx, const char *l) { (void)ctx; puts(l); fflush(stdout); }

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

int main(int argc, char **argv) {
    const char *pattern = NULL; int a = 1;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (!strcmp(argv[a], "-r")) raw = 1;
        else if (!strcmp(argv[a], "-p") && a + 1 < argc) pattern = argv[++a];
        else { fprintf(stderr, "usage: %s [-r] [-p pattern.syx] handshake.syx\n", argv[0]); return 2; }
    }
    if (a >= argc) { fprintf(stderr, "need handshake.syx\n"); return 2; }
    snd_rawmidi_t *in[2] = {0}, *outp = NULL, *outc = NULL;
    const char *names[2] = {PORT_PUBLIC, PORT_PRIVATE};
    int e;
    if ((e = snd_rawmidi_open(&in[0], &outp, PORT_PUBLIC, SND_RAWMIDI_NONBLOCK)) < 0 ||
        (e = snd_rawmidi_open(&in[1], &outc, PORT_PRIVATE, SND_RAWMIDI_NONBLOCK)) < 0) {
        fprintf(stderr, "open %s: %s (is MPC stopped?)\n", names[in[0] ? 1 : 0], snd_strerror(e)); return 1;
    }
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
    size_t n; uint8_t *hs = slurp(argv[a], &n); if (!hs) return 1;
    send_sysex_file(outp, hs, n); free(hs);
    fprintf(stderr, "handshake sent\n");
    if (pattern) { uint8_t *p = slurp(pattern, &n); if (p) { send_sysex_file(outp, p, n); free(p); } }

    fl_parser ps[2]; fl_parser_init(&ps[0]); fl_parser_init(&ps[1]);
    int np = snd_rawmidi_poll_descriptors_count(in[0]) + snd_rawmidi_poll_descriptors_count(in[1]);
    struct pollfd pfd[8]; (void)np;
    int c0 = snd_rawmidi_poll_descriptors(in[0], pfd, 4);
    int c1 = snd_rawmidi_poll_descriptors(in[1], pfd + c0, 4);
    pfd[c0 + c1].fd = 0; pfd[c0 + c1].events = POLLIN;
    io_t io = {outp, outc};
    time_t last_ping = 0; char lb[1024]; size_t ll = 0;
    while (!stop) {
        if (time(NULL) - last_ping >= 4) {
            static const uint8_t ping[] = {0xF0, 0x47, 0x00, 0x40, 0x52, 0x00, 0x04, 0x31, 0x39, 0x16, 0x40, 0xF7};
            send_all(outp, ping, sizeof ping); last_ping = time(NULL);
        }
        poll(pfd, c0 + c1 + 1, 200);
        for (int i = 0; i < 2; i++) {
            uint8_t b[256]; ssize_t r;
            while ((r = snd_rawmidi_read(in[i], b, sizeof b)) > 0) {
                if (raw) { fprintf(stderr, "RX%d:", i); for (ssize_t k = 0; k < r; k++) fprintf(stderr, " %02X", b[k]); fputc('\n', stderr); }
                fl_parser_feed(&ps[i], i, b, (size_t)r, out_line, NULL);
            }
        }
        if (pfd[c0 + c1].revents & POLLIN) {
            ssize_t r = read(0, lb + ll, sizeof lb - ll - 1);
            if (r == 0) { pfd[c0 + c1].fd = -1; }
            else if (r > 0) {
                ll += r; lb[ll] = 0; char *nl;
                while ((nl = strchr(lb, '\n'))) {
                    *nl = 0; if (fl_cmd(lb, send_cb, &io) == 1) stop = 1;
                    memmove(lb, nl + 1, strlen(nl + 1) + 1); ll = strlen(lb);
                }
            }
        }
    }
    snd_rawmidi_close(in[0]); snd_rawmidi_close(in[1]); snd_rawmidi_close(outp); snd_rawmidi_close(outc);
    return 0;
}
