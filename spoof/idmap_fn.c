/* Generated from forcespoof.c (idmap) for idmap_test.c: regenerate after changing idmap. */
#include <stddef.h>
typedef struct { int pos; int kind; } idmap_t;
void idmap(idmap_t *st, unsigned char *b, size_t n, unsigned char from, unsigned char to)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char c = b[i];
		if (c == 0xF0) { st->pos = 0; st->kind = 0; continue; }
		if (st->pos < 0) continue;
		if (c == 0xF7 || (c & 0x80)) { st->pos = -1; continue; }
		st->pos++;
		if (st->pos == 1) st->kind = c == 0x47 ? 1 : c == 0x7E ? 2 : 0;
		else if (st->kind == 1 && st->pos == 2 && c != 0x7F && c != 0x00) st->kind = 0;
		else if (st->kind == 1 && st->pos == 3) { if (c == from) b[i] = to; st->kind = 0; }
		else if (st->kind == 2 && ((st->pos == 3 && c != 0x06) || (st->pos == 4 && c != 0x02) || (st->pos == 5 && c != 0x47))) st->kind = 0;
		else if (st->kind == 2 && st->pos == 6) { if (c == from) b[i] = to; st->kind = 0; }
	}
}

