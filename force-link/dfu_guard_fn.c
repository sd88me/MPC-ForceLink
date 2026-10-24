/* Generated from flnet.c (dfu_guard) for dfu_guard_test.c: regenerate after changing it. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

