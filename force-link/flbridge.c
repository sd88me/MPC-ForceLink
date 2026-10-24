// flbridge: relay the Force controller's Public/Private rawmidi ports to a USB MIDI gadget (the Live III sees the
// gadget's two subdevices as its Public/Private). Run with the Force's own MPC stopped.
//   flbridge [-r] <gadget-card> [<controller-card>=1]
// Gadget side hw:G,0,0 = Public, hw:G,0,1 = Private. Writes towards the controller are paced per sysex message.
#include <alsa/asoundlib.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

typedef struct { snd_rawmidi_t *in, *out; const char *tag; int pace; unsigned long bytes; } leg_t;

static int open_in(snd_rawmidi_t **h, const char *n) { return snd_rawmidi_open(h, NULL, n, SND_RAWMIDI_NONBLOCK); }
static int open_out(snd_rawmidi_t **h, const char *n) { return snd_rawmidi_open(NULL, h, n, 0); }

static void put(leg_t *l, const uint8_t *b, size_t n) {
	while (n) {
		size_t k = n;
		if (l->pace) {                       // one sysex (or up to 256 bytes of it) per write, then drain and gap
			size_t i = 0;
			while (i < k && b[i] != 0xF7 && i < 255) i++;
			k = i < k ? i + 1 : k;
		}
		ssize_t w = snd_rawmidi_write(l->out, b, k);
		if (w == -EAGAIN) { usleep(1000); continue; }
		if (w < 0) { fprintf(stderr, "%s write: %s\n", l->tag, snd_strerror((int)w)); return; }
		if (l->pace) { snd_rawmidi_drain(l->out); if (b[w - 1] == 0xF7) usleep(3000); }
		b += w; n -= (size_t)w;
	}
}

int main(int argc, char **argv) {
	int raw = 0, a = 1;
	if (a < argc && !strcmp(argv[a], "-r")) { raw = 1; a++; }
	if (a >= argc) { fprintf(stderr, "usage: %s [-r] <gadget-card> [<controller-card>]\n", argv[0]); return 2; }
	int g = atoi(argv[a++]), c = a < argc ? atoi(argv[a]) : 1;
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

	leg_t leg[4] = {{0}};      // 0,1: gadget->controller public/private; 2,3: controller->gadget
	char gn[2][32], cn[2][32], t[4][32];
	for (int p = 0; p < 2; p++) {
		snprintf(gn[p], sizeof gn[p], "hw:%d,0,%d", g, p);
		snprintf(cn[p], sizeof cn[p], "hw:%d,0,%d", c, p);
		snprintf(t[p], sizeof t[p], "%s MPC>ctl", p ? "priv" : "pub");
		snprintf(t[p + 2], sizeof t[p + 2], "%s ctl>MPC", p ? "priv" : "pub");
		leg[p].tag = t[p]; leg[p + 2].tag = t[p + 2]; leg[p].pace = 1;
		int e;
		if ((e = open_in(&leg[p].in, gn[p])) || (e = open_out(&leg[p + 2].out, gn[p])) ||
		    (e = open_in(&leg[p + 2].in, cn[p])) || (e = open_out(&leg[p].out, cn[p]))) {
			fprintf(stderr, "open port %d: %s\n", p, snd_strerror(e)); return 1;
		}
	}
	struct pollfd pf[4]; int pn[4], np;
	uint8_t buf[512];
	fprintf(stderr, "flbridge: gadget card %d <-> controller card %d\n", g, c);
	while (!stop) {
		np = 0;
		for (int i = 0; i < 4; i++) {
			int n = snd_rawmidi_poll_descriptors(leg[i].in, &pf[np], 1);
			if (n == 1) pn[np++] = i;
		}
		if (poll(pf, np, 500) <= 0) continue;
		for (int k = 0; k < np; k++) {
			unsigned short rev; snd_rawmidi_poll_descriptors_revents(leg[pn[k]].in, &pf[k], 1, &rev);
			if (!(rev & POLLIN)) continue;
			leg_t *l = &leg[pn[k]];
			ssize_t r = snd_rawmidi_read(l->in, buf, sizeof buf);
			if (r <= 0) continue;
			l->bytes += (unsigned long)r;
			if (raw) { fprintf(stderr, "%s:", l->tag); for (ssize_t i = 0; i < r && i < 24; i++) fprintf(stderr, " %02x", buf[i]); fprintf(stderr, r > 24 ? " ...\n" : "\n"); }
			put(l, buf, (size_t)r);
		}
	}
	for (int i = 0; i < 4; i++) fprintf(stderr, "%s: %lu bytes\n", leg[i].tag, leg[i].bytes);
	return 0;
}
