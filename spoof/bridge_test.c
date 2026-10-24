/* Offline test of the Live III bridge translation in forcespoof.c (x86, no device):
 *   gcc -DBRIDGE_TEST -o bt bridge_test.c -ldl -lpthread && ./bt
 * Feeds Live III input bytes and Force LED output through bridge_feed/bridge_led and checks what MPC (Force events) and
 * the Live III (LED messages) would receive. */
#define _GNU_SOURCE
#include <stddef.h>
#include <sys/types.h>
static void bridge_test_out(int kind, const unsigned char *m, size_t n);
#include "forcespoof.c"

static unsigned char led[2][256];
static size_t led_n[2];

static void bridge_test_out(int kind, const unsigned char *m, size_t n)
{
	memcpy(led[kind] + led_n[kind], m, n);
	led_n[kind] += n;
}

static int fails;

static void expect(const char *name, const unsigned char *got, size_t gn, const unsigned char *want, size_t wn)
{
	int ok = gn == wn && !memcmp(got, want, wn);
	printf("%-40s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		printf("   got :"); for (size_t i = 0; i < gn; i++) printf(" %02x", got[i]);
		printf("\n   want:"); for (size_t i = 0; i < wn; i++) printf(" %02x", want[i]);
		printf("\n");
		fails++;
	}
}

/* run Live III input through the bridge; return what MPC would read */
static size_t through(const unsigned char *in, size_t n, unsigned char *out)
{
	int sv[2];
	socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv);
	thr_fd[1] = sv[1];
	inparse_t ps = {{0}, 0, 3, 0};
	bridge_feed(&ps, in, (ssize_t)n);
	ssize_t r = recv(sv[0], out, 256, MSG_DONTWAIT);
	close(sv[0]); close(sv[1]); thr_fd[1] = -1;
	return r < 0 ? 0 : (size_t)r;
}

int main(void)
{
	unsigned char out[256];
	size_t n;

	/* pad 1 (bottom-left, note 0x25) -> Force row 7 col 0 = note 54+56 = 110; pad 16 (0x35) -> row 4 col 3 = 89 */
	const unsigned char pads[] = {0x99, 0x25, 0x64, 0xa9, 0x25, 0x40, 0x89, 0x25, 0x00, 0x99, 0x35, 0x7f};
	const unsigned char padsw[] = {0x99, 110, 0x64, 0xa9, 110, 0x40, 0x89, 110, 0x00, 0x99, 89, 0x7f};
	n = through(pads, sizeof pads, out);
	expect("pads 1 and 16 + aftertouch + off", out, n, padsw, sizeof padsw);

	/* PLAY stays PLAY, MAIN -> MATRIX, step 1 -> track select 1, step 9 -> launch 1, SET ignored */
	const unsigned char btn[] = {0x90, 0x52, 0x7f, 0x80, 0x52, 0x00, 0x90, 0x34, 0x7f, 0x90, 0x5c, 0x7f,
				     0x90, 0x64, 0x7f, 0x90, 0x0a, 0x7f};
	const unsigned char btnw[] = {0x90, 0x52, 0x7f, 0x80, 0x52, 0x00, 0x90, 0x03, 0x7f, 0x90, 0x60, 0x7f,
				      0x90, 0x38, 0x7f};
	n = through(btn, sizeof btn, out);
	expect("buttons mapped, unmapped dropped", out, n, btnw, sizeof btnw);

	/* knob 2: touch (0x55) -> 0x54; turns +1 +1 -1 -> running count 1 2 1; encoder passes through */
	const unsigned char kn[] = {0x90, 0x55, 0x7f, 0xb0, 0x11, 0x01, 0xb0, 0x11, 0x01, 0xb0, 0x11, 0x7f,
				    0x80, 0x55, 0x00, 0xb0, 0x64, 0x7f};
	const unsigned char knw[] = {0x90, 0x54, 0x7f, 0xb0, 0x11, 0x01, 0xb0, 0x11, 0x02, 0xb0, 0x11, 0x01,
				     0x80, 0x54, 0x00, 0xb0, 0x64, 0x7f};
	n = through(kn, sizeof kn, out);
	expect("knob touch + running count + encoder", out, n, knw, sizeof knw);

	/* heartbeat reply sysex from the Live III is dropped, running status handled */
	const unsigned char hb[] = {0xf0, 0x47, 0x7f, 0x58, 0x53, 0x00, 0x05, 0x13, 0x75, 0x21, 0x69, 0x01, 0xf7,
				    0x90, 0x52, 0x7f, 0x52, 0x00};
	const unsigned char hbw[] = {0x90, 0x52, 0x7f, 0x90, 0x52, 0x00};
	n = through(hb, sizeof hb, out);
	expect("sysex dropped, running status", out, n, hbw, sizeof hbw);

	/* LEDs: Force pad idx 56 (row 7 col 0) -> Live III pad LED 0; idx 0 (top row) ignored; PLAY LED CC passes */
	const unsigned char fl[] = {0xf0, 0x47, 0x7f, 0x40, 0x65, 0x00, 0x04, 56, 0x7f, 0x00, 0x10, 0xf7,
				    0xf0, 0x47, 0x7f, 0x40, 0x65, 0x00, 0x04, 0, 0x7f, 0x7f, 0x7f, 0xf7};
	const unsigned char flw[] = {0xf0, 0x47, 0x7f, 0x58, 0x65, 0x00, 0x04, 0, 0x7f, 0x00, 0x10, 0xf7};
	bridge_led(0, fl, sizeof fl);
	expect("pad LED to Live III, outside 4x4 ignored", led[0], led_n[0], flw, sizeof flw);

	const unsigned char bl[] = {0xb0, 0x52, 0x03, 0xb0, 0x03, 0x01, 0xb0, 0x7e, 0x01};
	const unsigned char blw[] = {0xb0, 0x52, 0x03, 0xb0, 0x34, 0x01};
	bridge_led(1, bl, sizeof bl);
	expect("button LEDs: PLAY, MATRIX->MAIN, unmapped dropped", led[1], led_n[1], blw, sizeof blw);

	/* map.conf: SET (0a) -> MASTER (05), PLAY ignored, LED value 3 -> 7, Force pad idx 0 -> Live III LED 0x20 */
	char conf[] = "/tmp/bridge_test_map.XXXXXX";
	int cfd = mkstemp(conf);
	const char *rules = "# test\nbtn 0a 05\nbtn 52 -\nled 3 7\nquad 0 20\nquad 3f 4f\nbogus line\n";
	if (write(cfd, rules, strlen(rules)) < 0) { /* checked by the results */ }
	close(cfd);
	load_map(conf);
	unlink(conf);
	const unsigned char ov[] = {0x90, 0x0a, 0x7f, 0x90, 0x52, 0x7f, 0x90, 0x51, 0x7f};
	const unsigned char ovw[] = {0x90, 0x05, 0x7f, 0x90, 0x51, 0x7f};
	n = through(ov, sizeof ov, out);
	expect("map.conf: btn override and ignore", out, n, ovw, sizeof ovw);

	led_n[0] = led_n[1] = 0;
	const unsigned char ql[] = {0xf0, 0x47, 0x7f, 0x40, 0x65, 0x00, 0x04, 0, 0x7f, 0x00, 0x00, 0xf7,
				    0xf0, 0x47, 0x7f, 0x40, 0x65, 0x00, 0x04, 56, 0x00, 0x7f, 0x00, 0xf7,
				    0xf0, 0x47, 0x7f, 0x40, 0x65, 0x00, 0x04, 0x3f, 0x00, 0x00, 0x7f, 0xf7};
	const unsigned char qlw[] = {0xf0, 0x47, 0x7f, 0x58, 0x65, 0x00, 0x04, 0x20, 0x7f, 0x00, 0x00, 0xf7,
				     0xf0, 0x47, 0x7f, 0x58, 0x65, 0x00, 0x04, 0x4f, 0x00, 0x00, 0x7f, 0xf7};
	bridge_led(0, ql, sizeof ql);
	expect("map.conf: quad mirror (unlisted pads off)", led[0], led_n[0], qlw, sizeof qlw);

	const unsigned char lv[] = {0xb0, 0x51, 0x03, 0xb0, 0x51, 0x01};
	const unsigned char lvw[] = {0xb0, 0x51, 0x07, 0xb0, 0x51, 0x01};
	bridge_led(1, lv, sizeof lv);
	expect("map.conf: LED value translation", led[1], led_n[1], lvw, sizeof lvw);

	puts(fails ? "FAILURES" : "ALL OK");
	return fails != 0;
}
