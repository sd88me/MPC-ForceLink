// flnet: serve the Force controller's Public/Private rawmidi ports over TCP (6760 = Public, 6761 = Private) so a Live III
// shim can use them as its own. Raw MIDI bytes both ways. Run with the Force's MPC stopped.
//   flnet [-r] [base-port]
// Towards the controller, writes are paced per sysex (drain + 3 ms), as forcelink does.
#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

static int find_card(void) {
	FILE *f = fopen("/proc/asound/cards", "r"); char l[256], id[32]; int n = -1, c;
	if (!f) return -1;
	while (fgets(l, sizeof l, f)) if (sscanf(l, " %d [%31[^] ]", &c, id) == 2 && !strcmp(id, "Force")) n = c;
	fclose(f); return n;
}

// Guard: never let an Akai "enter DFU" command (F0 47 <00|7F> <id> 72 ...) reach the controller. Buffers the first five
// bytes of every sysex until the command byte is known, then passes or drops the whole message. One state per port.
typedef struct { uint8_t head[5]; int n; int drop; } dfu_guard_t;

size_t dfu_guard(dfu_guard_t *g, const uint8_t *in, size_t n, uint8_t *out) {
	size_t o = 0;
	for (size_t i = 0; i < n; i++) {
		uint8_t c = in[i];
		if (c == 0xF0) {                          // new sysex: flush an unfinished head as-is, start buffering
			if (g->n > 0 && !g->drop) { memcpy(out + o, g->head, (size_t)g->n); o += (size_t)g->n; }
			g->head[0] = c; g->n = 1; g->drop = 0;
			continue;
		}
		if (g->drop) { if (c == 0xF7) { g->drop = 0; g->n = 0; } continue; }
		if (g->n > 0 && g->n < 5 && !(c & 0x80)) {
			g->head[g->n++] = c;
			if (g->n == 5) {
				if (g->head[1] == 0x47 && g->head[4] == 0x72) { g->drop = 1; fprintf(stderr, "blocked a DFU command\n"); continue; }
				memcpy(out + o, g->head, 5); o += 5; g->n = -1;   // -1: inside a passing sysex
			}
			continue;
		}
		if (g->n > 0) { memcpy(out + o, g->head, (size_t)g->n); o += (size_t)g->n; g->n = 0; }   // short sysex ended
		if (c == 0xF7 || (c & 0x80)) g->n = 0;
		out[o++] = c;
	}
	return o;
}

static void put_ctl(snd_rawmidi_t *o, const uint8_t *b, size_t n) {
	while (n) {
		size_t k = 0;
		while (k < n && b[k] != 0xF7 && k < 255) k++;
		k = k < n ? k + 1 : n;
		ssize_t w = snd_rawmidi_write(o, b, k);
		if (w == -EAGAIN) { usleep(1000); continue; }
		if (w < 0) return;
		snd_rawmidi_drain(o);
		if (b[w - 1] == 0xF7) usleep(3000);
		b += w; n -= (size_t)w;
	}
}

int main(int argc, char **argv) {
	int raw = 0, a = 1, base = 6760;
	if (a < argc && !strcmp(argv[a], "-r")) { raw = 1; a++; }
	if (a < argc) base = atoi(argv[a]);
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGPIPE, SIG_IGN);
	int card = find_card();
	if (card < 0) { fprintf(stderr, "no Force controller card\n"); return 1; }
	snd_rawmidi_t *in[2], *out[2]; int ls[3], cl[3] = {-1, -1, -1};
	for (int p = 0; p < 2; p++) {
		char n[32]; snprintf(n, sizeof n, "hw:%d,0,%d", card, p);
		int e = snd_rawmidi_open(&in[p], NULL, n, SND_RAWMIDI_NONBLOCK);
		if (!e) e = snd_rawmidi_open(NULL, &out[p], n, 0);
		if (e) { fprintf(stderr, "open %s: %s\n", n, snd_strerror(e)); return 1; }
		ls[p] = socket(AF_INET, SOCK_STREAM, 0);
		int one = 1; setsockopt(ls[p], SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(base + p), .sin_addr.s_addr = htonl(INADDR_ANY)};
		if (bind(ls[p], (void *)&sa, sizeof sa) || listen(ls[p], 1)) { perror("bind"); return 1; }
	}
	ls[2] = socket(AF_INET, SOCK_STREAM, 0);
	{
		int one = 1; setsockopt(ls[2], SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(base + 2), .sin_addr.s_addr = htonl(INADDR_ANY)};
		if (bind(ls[2], (void *)&sa, sizeof sa) || listen(ls[2], 1)) { perror("bind side"); return 1; }
	}
	fprintf(stderr, "flnet: controller card %d, ports %d/%d, side %d (copy of controller port 1 in, writes to port 0)\n", card, base, base + 1, base + 2);
	uint8_t buf[1024], gbuf[1024 + 8]; unsigned long up[2] = {0}, down[2] = {0};
	dfu_guard_t guard[3] = {{{0}, 0, 0}, {{0}, 0, 0}, {{0}, 0, 0}};
	while (!stop) {
		struct pollfd pf[10]; int np = 0, kind[10], pi[10];
		for (int p = 0; p < 2; p++) {
			pf[np] = (struct pollfd){ls[p], POLLIN, 0}; kind[np] = 0; pi[np++] = p;
			if (cl[p] >= 0) { pf[np] = (struct pollfd){cl[p], POLLIN, 0}; kind[np] = 1; pi[np++] = p; }
			struct pollfd m; if (snd_rawmidi_poll_descriptors(in[p], &m, 1) == 1) { m.events = POLLIN; pf[np] = m; kind[np] = 2; pi[np++] = p; }
		}
		pf[np] = (struct pollfd){ls[2], POLLIN, 0}; kind[np] = 3; pi[np++] = 2;
		if (cl[2] >= 0) { pf[np] = (struct pollfd){cl[2], POLLIN, 0}; kind[np] = 4; pi[np++] = 2; }
		if (poll(pf, np, 500) <= 0) continue;
		for (int k = 0; k < np; k++) {
			int p = pi[k];
			if (kind[k] == 3 && (pf[k].revents & POLLIN)) {
				int c = accept(ls[2], NULL, NULL); if (c < 0) continue;
				int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
				if (cl[2] >= 0) close(cl[2]);
				cl[2] = c; fprintf(stderr, "side: helper connected\n");
			} else if (kind[k] == 4 && (pf[k].revents & (POLLIN | POLLHUP | POLLERR))) {
				ssize_t r = read(cl[2], buf, sizeof buf);
				if (r <= 0) { close(cl[2]); cl[2] = -1; fprintf(stderr, "side: helper gone\n"); continue; }
				put_ctl(out[0], gbuf, dfu_guard(&guard[2], buf, (size_t)r, gbuf));
			} else if (kind[k] == 0 && (pf[k].revents & POLLIN)) {
				int c = accept(ls[p], NULL, NULL); if (c < 0) continue;
				int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
				if (cl[p] >= 0) close(cl[p]);
				cl[p] = c; fprintf(stderr, "port %d: client connected\n", p);
			} else if (kind[k] == 1 && (pf[k].revents & (POLLIN | POLLHUP | POLLERR))) {
				ssize_t r = read(cl[p], buf, sizeof buf);
				if (r <= 0) { close(cl[p]); cl[p] = -1; fprintf(stderr, "port %d: client gone\n", p); continue; }
				up[p] += (unsigned long)r;
				if (raw) { fprintf(stderr, "p%d MPC>ctl %zd:", p, r); for (ssize_t i = 0; i < r && i < 16; i++) fprintf(stderr, " %02x", buf[i]); fputc('\n', stderr); }
				put_ctl(out[p], gbuf, dfu_guard(&guard[p], buf, (size_t)r, gbuf));
			} else if (kind[k] == 2 && (pf[k].revents & POLLIN)) {
				ssize_t r = snd_rawmidi_read(in[p], buf, sizeof buf);
				if (r <= 0) continue;
				down[p] += (unsigned long)r;
				if (raw) { fprintf(stderr, "p%d ctl>MPC %zd:", p, r); for (ssize_t i = 0; i < r && i < 16; i++) fprintf(stderr, " %02x", buf[i]); fputc('\n', stderr); }
				if (cl[p] >= 0 && write(cl[p], buf, (size_t)r) < 0) { close(cl[p]); cl[p] = -1; }
				if (p == 1 && cl[2] >= 0 && write(cl[2], buf, (size_t)r) < 0) { close(cl[2]); cl[2] = -1; }
			}
		}
	}
	for (int p = 0; p < 2; p++) fprintf(stderr, "port %d: up %lu down %lu\n", p, up[p], down[p]);
	return 0;
}
