// offline test of flnet's dfu_guard() (copy in dfu_guard_fn.c): gcc -o t dfu_guard_test.c dfu_guard_fn.c && ./t
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint8_t head[5]; int n; int drop; } dfu_guard_t;
size_t dfu_guard(dfu_guard_t *g, const uint8_t *in, size_t n, uint8_t *out);
static int t(const char *name, const uint8_t *in, size_t n, const uint8_t *want, size_t wn)
{
	int ok = 1;
	for (size_t sp = 0; sp <= n; sp++) {
		dfu_guard_t g = {{0}, 0, 0}; uint8_t out[256]; size_t o = 0;
		o += dfu_guard(&g, in, sp, out + o);
		o += dfu_guard(&g, in + sp, n - sp, out + o);
		if (o != wn || memcmp(out, want, wn)) { ok = 0; printf("  split %zu: got %zu bytes\n", sp, o); }
	}
	printf("%-36s %s\n", name, ok ? "ok" : "FAIL");
	return ok;
}
int main(void)
{
	int ok = 1;
	const uint8_t pad[] = {0xF0,0x47,0x7F,0x40,0x65,0x00,0x04,0x01,0x7F,0x00,0x00,0xF7};
	ok &= t("pad colour passes", pad, sizeof pad, pad, sizeof pad);
	const uint8_t dfu[] = {0xB0,0x35,0x01, 0xF0,0x47,0x00,0x40,0x72,0x00,0x00,0xF7, 0x99,0x40,0x7F};
	const uint8_t dfuw[] = {0xB0,0x35,0x01, 0x99,0x40,0x7F};
	ok &= t("DFU dropped, neighbours kept", dfu, sizeof dfu, dfuw, sizeof dfuw);
	const uint8_t id[] = {0xF0,0x7E,0x00,0x06,0x01,0xF7};
	ok &= t("identity request passes", id, sizeof id, id, sizeof id);
	const uint8_t shortx[] = {0xF0,0x01,0xF7,0x90,0x01,0x7F};
	ok &= t("short sysex passes", shortx, sizeof shortx, shortx, sizeof shortx);
	puts(ok ? "ALL OK" : "FAILURES");
	return !ok;
}
