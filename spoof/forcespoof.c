/* Device-identity spoof (Live III -> Force/ADA2) + Public/Private rawmidi ports carried over TCP to a Force running flnet
 * (net.conf: the Force's address). Opens of the devicetree files below return a memfd with the Force's value, or ENOENT;
 * the real files are never written. dt.conf picks how far: absent/"0" product code only, "a" also `compatible` with the
 * product swapped (board kept: az04), "b" the Force's az01 board identity too, "s" product ADA2S (Force Gen 2, same az04
 * board as the Live III) with the controller id translated 0x04 <-> 0x40 on the link (see idmap), "n" no spoofing at all
 * (with tap.conf: capture the stock Live III controller traffic). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DT "/sys/firmware/devicetree/base/"
#define HIDE ((const char *)1)
static const struct { char mode; const char *name, *val; size_t len; } spoofs[] = {
	{'0', DT "inmusic,product-code", "ADA2", 5},
	{'s', DT "inmusic,product-code", "ADA2S", 6},   /* Force (Gen 2): az04 board like the Live III, controller id 0x04 */
	{'a', DT "compatible", "inmusic,ada2\0inmusic,az04\0rockchip,rk3588", 42},
	{'b', DT "compatible", "inmusic,ada2\0inmusic,az01\0rockchip,rk3288-c\0rockchip,rk3288", 60},
	{'b', DT "inmusic,az01-pcb-rev", "D", 2},
	{'b', DT "inmusic,az04-pcb-rev", HIDE, 0},
	{'b', DT "inmusic,az04-pcb-suffix", HIDE, 0},
};

static char mode(void)
{
	static char m;
	if (!m) {
		FILE *f = fopen("/data/mpc-addins/forcespoof/dt.conf", "r");
		int c = f ? fgetc(f) : EOF;
		if (f)
			fclose(f);
		m = (c == 'a' || c == 'b' || c == 's' || c == 'n') ? (char)c : '0';
	}
	return m;
}

/* the spoof entry for this path in the current mode, or -1 */
static int is_code(const char *p)
{
	static __thread int busy;            /* the checks below open files themselves: no re-entry */
	static int want = -1;
	if (p && !busy && (strstr(p, "Devices") || strstr(p, ".lua") || strstr(p, ".ini"))) {
		busy = 1;
		if (want < 0) {
			FILE *c = fopen("/data/mpc-addins/forcespoof/scriptlog.conf", "r");
			want = c != NULL;
			if (c) fclose(c);
		}
		FILE *l = want ? fopen("/tmp/forcespoof.log", "a") : NULL;   /* where MPC looks for controller scripts */
		if (l) { fprintf(l, "script path %s\n", p); fclose(l); }
		busy = 0;
	}
	if (!p || strncmp(p, DT, sizeof DT - 1))
		return -1;
	char m = mode();
	int hit = -1;
	FILE *l = fopen("/tmp/forcespoof.log", "a");     /* every devicetree read, to learn what MPC looks at */
	if (l) { fprintf(l, "dt read %s\n", p); fclose(l); }
	for (int i = 0; i < (int)(sizeof spoofs / sizeof *spoofs); i++)
		if (!strcmp(p, spoofs[i].name) && ((spoofs[i].mode == '0' && m != 'n') || spoofs[i].mode == m))
			hit = i;                        /* later entries (mode b) win over earlier ones */
	return hit;
}

/* a memfd with the spoofed value (>= 0), or -1 with errno ENOENT for a hidden file */
static int fake(const char *via, const char *p, int i)
{
	FILE *l = fopen("/tmp/forcespoof.log", "a");
	if (l) {
		fprintf(l, "%s(%s) -> %s (pid %d)\n", via, p, spoofs[i].val == HIDE ? "ENOENT" : "spoofed", getpid());
		fclose(l);
	}
	if (spoofs[i].val == HIDE) {
		errno = ENOENT;
		return -1;
	}
	int fd = memfd_create("dt", 0);
	if (fd < 0)
		return -1;
	if (write(fd, spoofs[i].val, spoofs[i].len) != (ssize_t)spoofs[i].len) {
		close(fd);
		return -1;
	}
	lseek(fd, 0, SEEK_SET);
	return fd;
}

int open(const char *p, int fl, ...)
{
	static int (*real)(const char *, int, ...);
	if (!real)
		real = dlsym(RTLD_NEXT, "open");
	int sp = is_code(p);
	if (sp >= 0) {
		int fd = fake("open", p, sp);
		if (fd >= 0 || spoofs[sp].val == HIDE)
			return fd;
	}
	mode_t m = 0;
	if (fl & (O_CREAT | O_TMPFILE)) {
		va_list a;
		va_start(a, fl);
		m = va_arg(a, mode_t);
		va_end(a);
	}
	return real(p, fl, m);
}

int open64(const char *p, int fl, ...)
{
	static int (*real)(const char *, int, ...);
	if (!real)
		real = dlsym(RTLD_NEXT, "open64");
	int sp = is_code(p);
	if (sp >= 0) {
		int fd = fake("open64", p, sp);
		if (fd >= 0 || spoofs[sp].val == HIDE)
			return fd;
	}
	mode_t m = 0;
	if (fl & (O_CREAT | O_TMPFILE)) {
		va_list a;
		va_start(a, fl);
		m = va_arg(a, mode_t);
		va_end(a);
	}
	return real(p, fl, m);
}

int openat(int d, const char *p, int fl, ...)
{
	static int (*real)(int, const char *, int, ...);
	if (!real)
		real = dlsym(RTLD_NEXT, "openat");
	int sp = is_code(p);
	if (sp >= 0) {
		int fd = fake("openat", p, sp);
		if (fd >= 0 || spoofs[sp].val == HIDE)
			return fd;
	}
	mode_t m = 0;
	if (fl & (O_CREAT | O_TMPFILE)) {
		va_list a;
		va_start(a, fl);
		m = va_arg(a, mode_t);
		va_end(a);
	}
	return real(d, p, fl, m);
}

int openat64(int d, const char *p, int fl, ...)
{
	static int (*real)(int, const char *, int, ...);
	if (!real)
		real = dlsym(RTLD_NEXT, "openat64");
	int sp = is_code(p);
	if (sp >= 0) {
		int fd = fake("openat64", p, sp);
		if (fd >= 0 || spoofs[sp].val == HIDE)
			return fd;
	}
	mode_t m = 0;
	if (fl & (O_CREAT | O_TMPFILE)) {
		va_list a;
		va_start(a, fl);
		m = va_arg(a, mode_t);
		va_end(a);
	}
	return real(d, p, fl, m);
}


/* Existence checks on the spoofed devicetree files (mode b: az01 board present, az04 board absent). A spoofed file that
 * does not really exist is stat'ed as the product-code file, which always exists. */
#include <sys/stat.h>
struct statx;

static const char *stat_target(const char *p, int *hidden)
{
	int sp = is_code(p);
	*hidden = 0;
	if (sp < 0)
		return p;
	if (spoofs[sp].val == HIDE) {
		*hidden = 1;
		return p;
	}
	return DT "inmusic,product-code";
}

#define EXIST_HOOK(ret, name, params, args_with_q)                                       \
	ret name params                                                                    \
	{                                                                                  \
		static ret (*real) params;                                                 \
		if (!real)                                                                 \
			real = dlsym(RTLD_NEXT, #name);                                    \
		int h;                                                                     \
		const char *q = stat_target(p, &h);                                        \
		if (h) {                                                                   \
			errno = ENOENT;                                                    \
			return -1;                                                         \
		}                                                                          \
		return real args_with_q;                                                   \
	}

EXIST_HOOK(int, access, (const char *p, int m), (q, m))
EXIST_HOOK(int, faccessat, (int d, const char *p, int m, int f), (d, q, m, f))
EXIST_HOOK(int, stat, (const char *p, struct stat *b), (q, b))
EXIST_HOOK(int, stat64, (const char *p, struct stat64 *b), (q, b))
EXIST_HOOK(int, lstat, (const char *p, struct stat *b), (q, b))
EXIST_HOOK(int, lstat64, (const char *p, struct stat64 *b), (q, b))
EXIST_HOOK(int, fstatat, (int d, const char *p, struct stat *b, int f), (d, q, b, f))
EXIST_HOOK(int, fstatat64, (int d, const char *p, struct stat64 *b, int f), (d, q, b, f))
EXIST_HOOK(int, statx, (int d, const char *p, int f, unsigned int m, struct statx *b), (d, q, f, m, b))

/* Port redirect over the network. When CONF holds the Force's address, the Live III's Public/Private rawmidi handles that
 * MPC opens are wrapped: info/params/name/stream go to the real port (so MPC identifies it as before), while
 * read/write/poll/status use a socketpair whose other end a thread relays to host:6760 (Public) / host:6761 (Private). */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

typedef struct _snd_rawmidi snd_rawmidi_t;
typedef struct { snd_rawmidi_t *real; int fd; int dir; } fake_t;  /* dir: 1 in, 2 out */

#define CONF "/data/mpc-addins/forcespoof/net.conf"
#define BASEPORT 6760
static fake_t *fakes[16];
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int mpc_fd[2] = {-1, -1}, thr_fd[2] = {-1, -1};
static const void *stat_ptr[8];
static int stat_fd[8], stat_n;
static char host[64];

static void *next_sym(const char *name)
{
	static void *(*libc_dlsym)(void *, const char *);
	if (!libc_dlsym) {
		static const char *const vers[] = {"2.34", "2.4", "2.17", "2.2.5", "2.0"};
		for (size_t i = 0; i < sizeof vers / sizeof *vers && !libc_dlsym; i++) {
			char v[16] = "GLIBC_";
			strcat(v, vers[i]);
			libc_dlsym = (void *(*)(void *, const char *))dlvsym(RTLD_DEFAULT, "dlsym", v);
		}
	}
	return (libc_dlsym ? libc_dlsym : dlsym)(RTLD_NEXT, name);
}

static void note(const char *a, const char *b)
{
	FILE *l = fopen("/tmp/forcespoof.log", "a");
	if (l) { fprintf(l, "%s %s\n", a, b ? b : ""); fclose(l); }
}

static fake_t *as_fake(snd_rawmidi_t *h)
{
	fake_t *f = NULL;
	pthread_mutex_lock(&mtx);
	for (int i = 0; i < 16; i++)
		if (fakes[i] && (void *)fakes[i] == (void *)h) { f = fakes[i]; break; }
	pthread_mutex_unlock(&mtx);
	return f;
}

static int read_host(void)
{
	FILE *f = fopen(CONF, "r");
	if (!f)
		return 0;
	host[0] = 0;
	if (fgets(host, sizeof host, f))
		host[strcspn(host, " \r\n")] = 0;
	fclose(f);
	return host[0] != 0;
}

static int dial(int kind)
{
	int s = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(BASEPORT + kind)};
	struct timeval tv = {2, 0};
	int one = 1;
	if (s < 0 || inet_pton(AF_INET, host, &a.sin_addr) != 1)
		return -1;
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	if (connect(s, (struct sockaddr *)&a, sizeof a)) { close(s); return -1; }
	return s;
}

/* Controller-id translation for mode s. MPC (as ADA2S) talks to controller id 0x04; the Force's Gen 1 controller is 0x40.
 * Rewrites the id byte of Akai sysex headers (F0 47 7F|00 <id>) and the family byte of the identity reply
 * (F0 7E <ch> 06 02 47 <id>). Stateful per direction, so headers split across reads are handled. */
typedef struct { int pos; int kind; } idmap_t;   /* pos: bytes since F0 (-1 outside sysex); kind: 1 akai, 2 identity */

static void idmap(idmap_t *st, unsigned char *b, size_t n, unsigned char from, unsigned char to)
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

/* The Live III's own controller handles, real (unwrapped), current per port; NULL once MPC closes them. nat_mtx guards
 * every use, so a read or write never races snd_rawmidi_close. */
static snd_rawmidi_t *nat_in[2], *nat_out[2];
static pthread_mutex_t nat_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Live III bridge hooks (defined further down) */
static int bridge_on(void);
static void bridge_led(int kind, const unsigned char *b, ssize_t n);
static pthread_mutex_t inj_mtx = PTHREAD_MUTEX_INITIALIZER;   /* MPC-side Private writes: relay vs bridge */
static int down_in_sysex;                                      /* the relay left a sysex unfinished on Private */

static void *relay(void *arg)
{
	int kind = (int)(long)arg, fd = thr_fd[kind], tcp = -1;
	unsigned char b[1024];
	int xl = mode() == 's';
	idmap_t up = {-1, 0}, down = {-1, 0};
	for (;;) {
		if (tcp < 0) {
			struct pollfd p = {fd, POLLIN, 0};
			if (poll(&p, 1, 1000) > 0 && read(fd, b, sizeof b) < 0 && errno != EAGAIN) { /* discard while offline */ }
			if ((tcp = dial(kind)) >= 0)
				note(kind ? "link up private" : "link up public", host);
			continue;
		}
		struct pollfd p[2] = {{fd, POLLIN, 0}, {tcp, POLLIN, 0}};
		if (poll(p, 2, 1000) <= 0)
			continue;
		if (p[0].revents & POLLIN) {
			ssize_t n = read(fd, b, sizeof b);
			if (n > 0 && xl)
				idmap(&up, b, (size_t)n, 0x04, 0x40);
			if (n > 0 && bridge_on())
				bridge_led(kind, b, n);
			if (n > 0 && send(tcp, b, (size_t)n, MSG_NOSIGNAL) < 0) { close(tcp); tcp = -1; note("link lost (send)", NULL); continue; }
		}
		if (p[1].revents & (POLLIN | POLLHUP | POLLERR)) {
			ssize_t n = read(tcp, b, sizeof b);
			if (n <= 0) { close(tcp); tcp = -1; note("link lost (recv)", NULL); continue; }
			if (xl)
				idmap(&down, b, (size_t)n, 0x40, 0x04);
			pthread_mutex_lock(&inj_mtx);
			if (write(fd, b, (size_t)n) < 0) { /* MPC side full: drop */ }
			if (kind == 1) {
				for (ssize_t i = 0; i < n; i++) {
					if (b[i] == 0xF0) down_in_sysex = 1;
					else if (b[i] == 0xF7) down_in_sysex = 0;
				}
			}
			pthread_mutex_unlock(&inj_mtx);
		}
	}
	return NULL;
}

static int ensure_relay(int kind)
{
	pthread_mutex_lock(&mtx);
	if (mpc_fd[kind] < 0) {
		int sv[2];
		if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv)) { pthread_mutex_unlock(&mtx); return -1; }
		mpc_fd[kind] = sv[0]; thr_fd[kind] = sv[1];
		pthread_t t;
		pthread_create(&t, NULL, relay, (void *)(long)kind);
		pthread_detach(t);
	}
	int fd = mpc_fd[kind];
	pthread_mutex_unlock(&mtx);
	return fd;
}

static fake_t *wrap(snd_rawmidi_t *real, int fd, int dir)
{
	fake_t *f = calloc(1, sizeof *f);
	f->real = real; f->fd = fd; f->dir = dir;
	pthread_mutex_lock(&mtx);
	for (int i = 0; i < 16; i++)
		if (!fakes[i]) { fakes[i] = f; break; }
	pthread_mutex_unlock(&mtx);
	return f;
}

static int port_kind(snd_rawmidi_t *h)
{
	size_t (*isz)(void) = next_sym("snd_rawmidi_info_sizeof");
	int (*info)(snd_rawmidi_t *, void *) = next_sym("snd_rawmidi_info");
	const char *(*sub)(const void *) = next_sym("snd_rawmidi_info_get_subdevice_name");
	const char *(*dev)(const void *) = next_sym("snd_rawmidi_info_get_name");
	int kind = -1;
	if (!isz || !info || !sub || !dev)
		return -1;
	void *i = calloc(1, isz());
	if (i && !info(h, i)) {
		const char *a = sub(i), *b = dev(i);
		if ((a && strstr(a, "Private")) || (b && strstr(b, "Private")))
			kind = 1;
		else if ((a && strstr(a, "Public")) || (b && strstr(b, "Public")))
			kind = 0;
	}
	free(i);
	return kind;
}


/* Native-protocol capture. tap.conf present and no net.conf: MPC keeps its own Live III controller, and every byte it
 * reads from or writes to the Public/Private ports is logged. With the network link on, a thread per port logs what the
 * Live III's own controller sends (its pads and buttons), which MPC no longer reads. Log: /tmp/native.log. */
#include <time.h>
static snd_rawmidi_t *tapped[8];
static int tapped_kind[8];

static int tap_on(void)
{
	static int t = -1;
	if (t < 0)
		t = access("/data/mpc-addins/forcespoof/tap.conf", F_OK) == 0;
	return t;
}

static void tlog(int kind, const char *what, const unsigned char *b, ssize_t n)
{
	static pthread_mutex_t lm = PTHREAD_MUTEX_INITIALIZER;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	pthread_mutex_lock(&lm);
	FILE *l = fopen("/tmp/native.log", "a");
	if (l) {
		fprintf(l, "%ld.%03ld %s %s %zd:", (long)ts.tv_sec, ts.tv_nsec / 1000000, kind ? "priv" : "pub", what, n);
		for (ssize_t i = 0; i < n && i < 600; i++)
			fprintf(l, " %02x", b[i]);
		fputs(n > 600 ? " ...\n" : "\n", l);
		fclose(l);
	}
	pthread_mutex_unlock(&lm);
}

static int tapped_as(snd_rawmidi_t *h)
{
	for (int i = 0; i < 8; i++)
		if (tapped[i] && tapped[i] == h)
			return tapped_kind[i];
	return -1;
}

static void tap_add(snd_rawmidi_t *h, int kind)
{
	pthread_mutex_lock(&mtx);
	for (int i = 0; i < 8; i++)
		if (!tapped[i]) { tapped[i] = h; tapped_kind[i] = kind; break; }
	pthread_mutex_unlock(&mtx);
}

/* Live III bridge (mode s + live3.conf). MPC drives the Force, but the Live III's own controller stays usable: the shim
 * brings it up itself (replays MPC's captured start-up, live3_init.syx, and sends the heartbeat), translates its pads,
 * buttons and knobs into Force events injected into MPC's Private stream, and mirrors MPC's Force LED output for the
 * mapped controls back onto it. Note numbers from the 2026-10-24 captures (FINDINGS §17). */
#define BRIDGE_DIR "/data/mpc-addins/forcespoof/"

static int bridge_on(void)
{
	static int b = -1;
	if (b < 0)
		b = mode() == 's' && access(BRIDGE_DIR "live3.conf", F_OK) == 0;
	return b;
}

/* Live III pads 1..16 (pad 1 bottom-left, rows upward) send these notes on channel 10 */
static const unsigned char l3_pad[16] = {0x25, 0x24, 0x2a, 0x52, 0x28, 0x26, 0x2e, 0x2c,
					 0x30, 0x2f, 0x2d, 0x2b, 0x31, 0x37, 0x33, 0x35};

/* Live III button note -> Force button note (both on channel 1); unlisted buttons are ignored */
static const unsigned char l3_btn[][2] = {
	{0x7b, 0x02}, /* MENU -> MENU */          {0x34, 0x03}, /* MAIN -> MATRIX */
	{0x31, 0x31}, /* SHIFT */                 {0x79, 0x04}, /* SOUNDS -> NOTE */
	{0x0c, 0x25}, /* EDIT */                  {0x70, 0x09}, /* ARRANGE -> CLIP */
	{0x21, 0x0b}, /* MIXER */                 {0x35, 0x35}, /* TAP TEMPO */
	{0x44, 0x72}, /* < -> LEFT */             {0x45, 0x73}, /* > -> RIGHT */
	{0x47, 0x70}, /* << -> UP */              {0x48, 0x71}, /* >> -> DOWN */
	{0x36, 0x36}, /* + */                     {0x37, 0x37}, /* - */
	{0x49, 0x49}, /* REC */                   {0x51, 0x51}, /* STOP */
	{0x52, 0x52}, /* PLAY */                  {0x0b, 0x76}, /* NOTE REPEAT -> ARP */
	{0x09, 0x26}, /* ERASE -> DELETE */       {0x7a, 0x7a}, /* COPY */
	{0x72, 0x5b}, /* MUTES -> MUTE */         {0x2c, 0x5b}, /* TRACK MUTE -> MUTE */
	{0x43, 0x43}, /* UNDO */                  {0x29, 0x23}, /* LOAD */
	{0x32, 0x24}, /* SAVE */                  {0x00, 0x01}, /* Q-LINK -> KNOBS */
	{0x23, 0x7b}, /* A -> ASSIGN A */         {0x24, 0x7c}, /* B -> ASSIGN B */
	{0x6f, 0x6f}, /* encoder push */
	{0x5c, 0x60}, {0x5d, 0x61}, {0x5e, 0x62}, {0x5f, 0x63}, /* steps 1-8 -> track select 1-8 */
	{0x60, 0x64}, {0x61, 0x65}, {0x62, 0x66}, {0x63, 0x67},
	{0x64, 0x38}, {0x65, 0x39}, {0x66, 0x3a}, {0x67, 0x3b}, /* steps 9-16 -> row launch 1-8 */
	{0x68, 0x3c}, {0x69, 0x3d}, {0x6a, 0x3e}, {0x6b, 0x3f},
};

/* Overrides from map.conf (see load_map): button map, LED value map, pad quadrant LEDs */
static short over_btn[128];          /* 0 = use the table, -1 = ignore, else force note + 1 */
static short over_led[128];          /* 0 = unchanged, else live value + 1 */
static short quad_led[64];           /* 0 = none, else live LED index + 1 */
static int have_quad;

static int map_btn(int live)
{
	if (live >= 0 && live < 128 && over_btn[live])
		return over_btn[live] < 0 ? -1 : over_btn[live] - 1;
	for (size_t i = 0; i < sizeof l3_btn / sizeof *l3_btn; i++)
		if (l3_btn[i][0] == live)
			return l3_btn[i][1];
	return -1;
}

static int unmap_btn(int force)   /* first Live III button for a Force button, for its LED */
{
	for (int l = 0; l < 128; l++)
		if (over_btn[l] > 0 && over_btn[l] - 1 == force)
			return l;
	for (size_t i = 0; i < sizeof l3_btn / sizeof *l3_btn; i++)
		if (l3_btn[i][1] == force)
			return l3_btn[i][0];
	return -1;
}

/* Force pad note for Live III pad p (0..15): the bottom-left 4x4 of the Force grid (Force row 0 = top) */
static int pad_force_note(int p)
{
	return 54 + (7 - p / 4) * 8 + p % 4;
}

/* inject a short message into MPC's Private input, never inside a sysex the relay is writing */
static void inject(const unsigned char *m, size_t n)
{
	for (int tries = 0; tries < 25; tries++) {
		pthread_mutex_lock(&inj_mtx);
		if (!down_in_sysex || tries == 24) {
			if (thr_fd[1] >= 0 && write(thr_fd[1], m, n) < 0) { /* MPC side full: drop */ }
			pthread_mutex_unlock(&inj_mtx);
			return;
		}
		pthread_mutex_unlock(&inj_mtx);
		usleep(2000);
	}
}

static void nat_write(int kind, const unsigned char *m, size_t n)
{
	static ssize_t (*wr)(snd_rawmidi_t *, const void *, size_t);
	static int (*dr)(snd_rawmidi_t *);
#ifdef BRIDGE_TEST
	bridge_test_out(kind, m, n);       /* bridge_test.c records what would go to the Live III */
	return;
#endif
	if (!wr) { wr = next_sym("snd_rawmidi_write"); dr = next_sym("snd_rawmidi_drain"); }
	pthread_mutex_lock(&nat_mtx);              /* MPC may close the handle meanwhile: see snd_rawmidi_close */
	if (nat_out[kind] && wr) {
		wr(nat_out[kind], m, n);
		if (dr)
			dr(nat_out[kind]);
	}
	pthread_mutex_unlock(&nat_mtx);
}

/* one complete Live III input message -> Force events */
static unsigned char knob_count[4];

static void bridge_input(const unsigned char *m, int n)
{
	unsigned char st = m[0] & 0xf0, ch = m[0] & 0x0f, o[3];
	if (ch == 9 && (st == 0x90 || st == 0x80 || st == 0xa0) && n == 3) {        /* pads */
		for (int p = 0; p < 16; p++)
			if (l3_pad[p] == m[1]) {
				o[0] = m[0]; o[1] = (unsigned char)pad_force_note(p); o[2] = m[2];
				inject(o, 3);
			}
		return;
	}
	if (ch != 0 || n != 3)
		return;
	if ((st == 0x90 || st == 0x80) && m[1] >= 0x54 && m[1] <= 0x57) {          /* Q-link touch */
		int k = m[1] - 0x54;
		if (st == 0x90 && m[2])
			knob_count[k] = 0;
		o[0] = m[0]; o[1] = (unsigned char)(0x53 + k); o[2] = m[2];
		inject(o, 3);
		return;
	}
	if (st == 0x90 || st == 0x80) {                                              /* buttons */
		int f = map_btn(m[1]);
		if (f >= 0) { o[0] = m[0]; o[1] = (unsigned char)f; o[2] = m[2]; inject(o, 3); }
		return;
	}
	if (st == 0xb0 && m[1] >= 0x10 && m[1] <= 0x13) {                            /* Q-link turns */
		int k = m[1] - 0x10, d = m[2] < 64 ? m[2] : m[2] - 128;               /* Live III: plain relative */
		knob_count[k] = (unsigned char)((knob_count[k] + d) & 0x7f);           /* Force: running count since touch */
		o[0] = 0xb0; o[1] = m[1]; o[2] = knob_count[k];
		inject(o, 3);
		return;
	}
	if (st == 0xb0 && m[1] == 0x64)                                              /* main encoder: same on both */
		inject(m, 3);
}

/* byte stream from the Live III controller -> complete messages (sysex replies are dropped: heartbeat answers) */
typedef struct { unsigned char m[3]; int n, need, in_sysex; } inparse_t;

static void bridge_feed(inparse_t *ps, const unsigned char *b, ssize_t n)
{
	for (ssize_t i = 0; i < n; i++) {
		unsigned char c = b[i];
		if (c == 0xF0) { ps->in_sysex = 1; continue; }
		if (c == 0xF7) { ps->in_sysex = 0; continue; }
		if (ps->in_sysex)
			continue;
		if (c & 0x80) {
			ps->m[0] = c; ps->n = 1;
			ps->need = (c & 0xf0) == 0xc0 || (c & 0xf0) == 0xd0 ? 2 : 3;
			continue;
		}
		if (ps->n == 0)
			continue;
		if (ps->n < 3)
			ps->m[ps->n++] = c;
		if (ps->n == ps->need) {
			bridge_input(ps->m, ps->n);
			ps->n = 1;                                   /* running status */
		}
	}
}

/* MPC's Force output (after the id rewrite) -> Live III LEDs: pad RGB for the bottom-left 4x4, button LED CCs */
typedef struct { unsigned char s[16]; int n, in_sysex; unsigned char st, m[2]; int mn; } ledparse_t;
static ledparse_t ledps[2];

static void bridge_led(int kind, const unsigned char *b, ssize_t n)
{
	ledparse_t *ps = &ledps[kind];
	for (ssize_t i = 0; i < n; i++) {
		unsigned char c = b[i];
		if (c == 0xF0) { ps->in_sysex = 1; ps->n = 0; ps->s[ps->n++] = c; continue; }
		if (ps->in_sysex) {
			if (ps->n < (int)sizeof ps->s)
				ps->s[ps->n++] = c;
			if (c != 0xF7)
				continue;
			ps->in_sysex = 0;
			/* F0 47 7F 40 65 00 04 idx r g b F7 */
			if (ps->n == 12 && ps->s[1] == 0x47 && ps->s[3] == 0x40 && ps->s[4] == 0x65) {
				int idx = ps->s[7], row = idx / 8, col = idx % 8;
				unsigned char o[12];
				memcpy(o, ps->s, 12);
				o[3] = 0x58;
				if (have_quad) {                       /* map.conf: every Force pad onto its quadrant */
					if (idx < 64 && quad_led[idx]) {
						o[7] = (unsigned char)(quad_led[idx] - 1);
						nat_write(0, o, 12);
					}
				} else if (idx < 64 && row >= 4 && col < 4) {
					o[7] = (unsigned char)((7 - row) * 4 + col);   /* Live III pad LED index (assumed pad order) */
					nat_write(0, o, 12);
				}
			}
			continue;
		}
		if (c & 0x80) { ps->st = c; ps->mn = 0; continue; }
		if (ps->st == 0xb0) {
			ps->m[ps->mn++] = c;
			if (ps->mn == 2) {
				int live = unmap_btn(ps->m[0]);
				if (live >= 0) {
					int v = ps->m[1] < 128 && over_led[ps->m[1]] ? over_led[ps->m[1]] - 1 : ps->m[1];
					unsigned char o[3] = {0xb0, (unsigned char)live, (unsigned char)v};
					nat_write(1, o, 3);
				}
				ps->mn = 0;
			}
		}
	}
}

/* map.conf, hex numbers, one rule per line, '#' comments:
 *   btn <live note> <force note|->   button mapping ('-' ignores the button)
 *   led <force value> <live value>   button LED value
 *   quad <force pad idx> <live LED>  mirror that Force pad (idx 0-3f, row 0 = top) onto a Live III LED */
static void load_map(const char *path)
{
	FILE *f = fopen(path, "r");
	char line[128], what[8], b[16];
	unsigned a;
	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		if (sscanf(line, "%7s %x %15s", what, &a, b) != 3 || what[0] == '#' || a > 127)
			continue;
		unsigned v = 0;
		int dash = !strcmp(b, "-");
		if (!dash && sscanf(b, "%x", &v) != 1)
			continue;
		if (!strcmp(what, "btn"))
			over_btn[a] = dash ? -1 : (short)(v + 1);
		else if (!strcmp(what, "led") && !dash)
			over_led[a] = (short)(v + 1);
		else if (!strcmp(what, "quad") && !dash && a < 64)
			quad_led[a] = (short)(v + 1), have_quad = 1;
	}
	fclose(f);
}

/* /tmp/l3probe: lines "pub|priv <hex bytes>", sent to the Live III controller as they are, then the file is removed.
 * For mapping LEDs by hand over SSH, e.g. "pub f0 47 7f 58 65 00 04 1c 7f 00 00 f7". */
static void run_probe(void)
{
	FILE *f = fopen("/tmp/l3probe", "r");
	char line[512];
	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		unsigned char m[160];
		size_t n = 0;
		int kind = !strncmp(line, "priv", 4) ? 1 : !strncmp(line, "pub", 3) ? 0 : -1;
		char *p = strchr(line, ' ');
		unsigned v;
		int used;
		if (kind < 0 || !p)
			continue;
		while (n < sizeof m && sscanf(p, " %x%n", &v, &used) == 1) {
			m[n++] = (unsigned char)v;
			p += used;
		}
		if (n)
			nat_write(kind, m, n);
	}
	fclose(f);
	unlink("/tmp/l3probe");
}

/* bring the Live III controller up and keep it alive (its heartbeat answers are ignored) */
static void *bridge_keepalive(void *arg)
{
	(void)arg;
	sleep(3);
	FILE *f = fopen(BRIDGE_DIR "live3_init.syx", "rb");
	if (f) {
		unsigned char b[2048];
		size_t n = fread(b, 1, sizeof b, f), s = 0;
		fclose(f);
		for (size_t i = 0; i < n; i++)
			if (b[i] == 0xF7) { nat_write(0, b + s, i - s + 1); usleep(3000); s = i + 1; }
		note("bridge: Live III controller initialised", NULL);
	}
	const unsigned char hb[] = {0xF0, 0x47, 0x00, 0x58, 0x52, 0x00, 0x04, 0x13, 0x75, 0x21, 0x69, 0xF7};
	for (int t = 0;; t++) {
		if (t % 30 == 0)
			nat_write(0, hb, sizeof hb);   /* every 3 s */
		run_probe();
		usleep(100000);
	}
	return NULL;
}

static void bridge_start(void)
{
	static int started;
	pthread_t t;
	if (started || !bridge_on() || !nat_out[0])
		return;
	started = 1;
	load_map(BRIDGE_DIR "map.conf");
	if (pthread_create(&t, NULL, bridge_keepalive, NULL) == 0)
		pthread_detach(t);
}


static void *native_reader(void *arg)
{
	int kind = (int)(long)arg;
	int (*pdesc)(snd_rawmidi_t *, struct pollfd *, unsigned int) = next_sym("snd_rawmidi_poll_descriptors");
	ssize_t (*rd)(snd_rawmidi_t *, void *, size_t) = next_sym("snd_rawmidi_read");
	int (*nb)(snd_rawmidi_t *, int) = next_sym("snd_rawmidi_nonblock");
	unsigned char b[512];
	inparse_t ps = {{0}, 0, 3, 0};
	snd_rawmidi_t *cur = NULL;
	struct pollfd p = {-1, POLLIN, 0};
	if (!pdesc || !rd)
		return NULL;
	for (;;) {
		pthread_mutex_lock(&nat_mtx);              /* follow the handle MPC currently has open */
		if (nat_in[kind] != cur) {
			cur = nat_in[kind];
			p.fd = -1;
			if (cur && pdesc(cur, &p, 1) == 1 && nb)
				nb(cur, 1);
		}
		pthread_mutex_unlock(&nat_mtx);
		if (!cur || p.fd < 0) {
			usleep(100000);
			continue;
		}
		p.events = POLLIN;
		if (poll(&p, 1, 200) <= 0 || (p.revents & POLLNVAL))
			continue;
		pthread_mutex_lock(&nat_mtx);
		ssize_t n = nat_in[kind] == cur ? rd(cur, b, sizeof b) : 0;
		pthread_mutex_unlock(&nat_mtx);
		if (n > 0 && tap_on())
			tlog(kind, "native-in", b, n);
		if (n > 0 && bridge_on())
			bridge_feed(&ps, b, n);
		else if (n < 0 && n != -EAGAIN)
			usleep(10000);
	}
	return NULL;
}

static void start_native_reader(snd_rawmidi_t *h, int kind)
{
	static int started[2];
	pthread_t t;
	pthread_mutex_lock(&nat_mtx);
	nat_in[kind] = h;
	pthread_mutex_unlock(&nat_mtx);
	if (started[kind])
		return;
	started[kind] = 1;                 /* one reader per port for the process; it follows nat_in */
	if (pthread_create(&t, NULL, native_reader, (void *)(long)kind) == 0)
		pthread_detach(t);
}

/* Version B (b.conf holds the Force's address): MPC keeps its own identity and controller; this adds an ALSA sequencer
 * port "Force Link" inside MPC (like the commander addin's port) and relays it to flsurf on the Force (TCP 6770).
 * A controller script (Devices/ForceLink.ini + .lua) binds to that port. */
typedef struct _snd_seq snd_seq_t;
typedef struct _snd_midi_event snd_midi_event_t;
typedef struct {
	unsigned char type, flags, tag, queue;
	struct { unsigned int sec, nsec; } time;
	struct { unsigned char client, port; } source, dest;
	unsigned char data[12];
} seq_ev_t;                                           /* snd_seq_event_t, 28 bytes since ALSA 1.0 */
_Static_assert(sizeof(seq_ev_t) == 28, "snd_seq_event_t layout");

static char b_host[64];

static int b_on(void)
{
	static int b = -1;
	if (b < 0) {
		FILE *f = fopen(BRIDGE_DIR "b.conf", "r");
		b = 0;
		if (f) {
			if (fgets(b_host, sizeof b_host, f)) {
				b_host[strcspn(b_host, " \r\n")] = 0;
				b = b_host[0] != 0;
			}
			fclose(f);
		}
	}
	return b;
}

static void *b_port_thread(void *arg)
{
	(void)arg;
	int (*sq_open)(snd_seq_t **, const char *, int, int) = next_sym("snd_seq_open");
	int (*sq_name)(snd_seq_t *, const char *) = next_sym("snd_seq_set_client_name");
	int (*sq_port)(snd_seq_t *, const char *, unsigned, unsigned) = next_sym("snd_seq_create_simple_port");
	int (*sq_nb)(snd_seq_t *, int) = next_sym("snd_seq_nonblock");
	int (*sq_in)(snd_seq_t *, seq_ev_t **) = next_sym("snd_seq_event_input");
	int (*sq_out)(snd_seq_t *, seq_ev_t *) = next_sym("snd_seq_event_output_direct");
	int (*sq_pd)(snd_seq_t *, struct pollfd *, unsigned, short) = next_sym("snd_seq_poll_descriptors");
	int (*me_new)(size_t, snd_midi_event_t **) = next_sym("snd_midi_event_new");
	int (*me_enc)(snd_midi_event_t *, int, seq_ev_t *) = next_sym("snd_midi_event_encode_byte");
	long (*me_dec)(snd_midi_event_t *, unsigned char *, long, const seq_ev_t *) = next_sym("snd_midi_event_decode");
	void (*me_nostatus)(snd_midi_event_t *, int) = next_sym("snd_midi_event_no_status");
	snd_seq_t *seq = NULL;
	snd_midi_event_t *enc = NULL, *dec = NULL;
	if (!sq_open || !sq_name || !sq_port || !sq_nb || !sq_in || !sq_out || !sq_pd || !me_new || !me_enc || !me_dec ||
	    sq_open(&seq, "default", 3 /* duplex */, 0) < 0) {
		note("B: no ALSA sequencer", NULL);
		return NULL;
	}
	sq_name(seq, "Force Link");
	int port = sq_port(seq, "Force Link", 1 | 2 | (1 << 5) | (1 << 6), (1 << 1) | (1 << 20));
	sq_nb(seq, 1);
	me_new(2048, &enc);
	me_new(2048, &dec);
	if (me_nostatus)
		me_nostatus(dec, 1);                      /* full status bytes towards flsurf */
	note("B: port Force Link open", b_host);
	int tcp = -1;
	unsigned char buf[2048];
	for (;;) {
		if (tcp < 0) {
			int s = socket(AF_INET, SOCK_STREAM, 0);
			struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(6770)};
			struct timeval tv = {2, 0};
			int one = 1;
			setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
			setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
			if (inet_pton(AF_INET, b_host, &a.sin_addr) == 1 && connect(s, (struct sockaddr *)&a, sizeof a) == 0) {
				tcp = s;
				note("B: link up", b_host);
			} else {
				close(s);
			}
		}
		struct pollfd p[5];
		int np = sq_pd(seq, p, 4, POLLIN);
		if (tcp >= 0)
			p[np++] = (struct pollfd){tcp, POLLIN, 0};
		if (poll(p, np, 1000) <= 0)
			continue;
		seq_ev_t *ev;
		while (sq_in(seq, &ev) >= 0 && ev) {         /* from the script: to the Force */
			long n = me_dec(dec, buf, sizeof buf, ev);
			if (n > 0 && tcp >= 0 && send(tcp, buf, (size_t)n, MSG_NOSIGNAL) < 0) {
				close(tcp); tcp = -1; note("B: link lost", NULL);
			}
		}
		if (tcp >= 0 && (p[np - 1].revents & (POLLIN | POLLHUP | POLLERR))) {
			ssize_t n = read(tcp, buf, sizeof buf);
			if (n <= 0) { close(tcp); tcp = -1; note("B: link lost", NULL); continue; }
			for (ssize_t i = 0; i < n; i++) {         /* from the Force: to the script */
				seq_ev_t e;
				memset(&e, 0, sizeof e);
				if (me_enc(enc, buf[i], &e) == 1) {
					e.source.port = (unsigned char)port;
					e.dest.client = 254;              /* subscribers */
					e.queue = 253;                    /* direct */
					sq_out(seq, &e);
				}
			}
		}
	}
	return NULL;
}

static void b_start(void)
{
	static int started;
	pthread_t t;
	if (started || !b_on())
		return;
	started = 1;
	if (pthread_create(&t, NULL, b_port_thread, NULL) == 0)
		pthread_detach(t);
}

/* ---- Setup switch: hold SHIFT + MENU, then MENU again (docs/DEVICE_SWITCH.md) ----------------------------------
 * Watches the Live III's own Private port in every setup (stock and B), blinks MENU while armed, tells the Force
 * (switch.conf peer) and runs link.sh detached. Inert without switch.conf. */
#include "../force-link/combo.h"
#include "../force-link/flswitch.h"
#define L3_SHIFT 0x31
#define L3_MENU 0x7b

static flsw_t sw;
static combo_t sw_combo;
static pthread_mutex_t sw_mtx = PTHREAD_MUTEX_INITIALIZER;
static snd_rawmidi_t *volatile sw_in, *volatile sw_out;   /* the Live III's own Private port, as MPC opened it */
static volatile int sw_ready, sw_armed, sw_menu_led = 1;           /* MENU LED value MPC last wrote, restored on cancel */

static uint32_t now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static void sw_led(int v)
{
	static ssize_t (*wr)(snd_rawmidi_t *, const void *, size_t);
	if (!wr)
		wr = next_sym("snd_rawmidi_write");
	snd_rawmidi_t *o = sw_out;
	unsigned char m[3] = {0xB0, L3_MENU, (unsigned char)v};
	if (o)
		wr(o, m, 3);
}

static void sw_action(int r)
{
	if (r == COMBO_ARMED) {
		sw_armed = 1;
		note("switch", "armed: press MENU to switch");
	} else if (r == COMBO_CANCEL) {
		sw_armed = 0;
		sw_led(sw_menu_led);
		note("switch", "cancelled");
	} else if (r == COMBO_SWITCH) {
		sw_armed = 0;
		/* the Force toggles; the Live III keeps its home setup (home.conf) or toggles too without one */
		note("switch:", flsw_tell_peer(&sw, "toggle") ? "the Force did not answer" : "the Force is toggling");
		sw_led(3);
		flsw_run_local(&sw, "toggle");
	}
}

static void *sw_thread(void *arg)
{
	(void)arg;
	for (unsigned i = 0;; i++) {
		usleep(100000);
		pthread_mutex_lock(&sw_mtx);
		int r = combo_tick(&sw_combo, now_ms());
		pthread_mutex_unlock(&sw_mtx);
		if (r)
			sw_action(r);
		if (sw_armed && (i & 1) == 0)
			sw_led((i & 2) ? 3 : 0);       /* blink ~2.5 Hz while armed */
	}
	return NULL;
}

static void sw_start(void)
{
	static int once;
	if (once)
		return;
	once = 1;
	if (flsw_load(&sw, BRIDGE_DIR "switch.conf"))
		return;                            /* not configured: no combo, no listener */
	combo_init(&sw_combo, L3_SHIFT, L3_MENU);
	sw_ready = 1;
	flsw_listen(&sw);
	pthread_t t;
	if (pthread_create(&t, NULL, sw_thread, NULL) == 0)
		pthread_detach(t);
	note("switch", "ready: hold SHIFT + MENU, then press MENU");
}

/* note on/off from the Live III's Private input (running status kept across reads) */
static void sw_feed(const unsigned char *b, ssize_t n)
{
	static unsigned char st, d[2];
	static int k;
	for (ssize_t i = 0; i < n; i++) {
		unsigned char c = b[i];
		if (c >= 0xF8)
			continue;                      /* realtime */
		if (c & 0x80) {
			st = c < 0xF0 ? c : 0;
			k = 0;
			continue;
		}
		if ((st & 0xE0) != 0x80)           /* only note on (9x) / off (8x) */
			continue;
		d[k++] = c;
		if (k < 2)
			continue;
		k = 0;
		int down = (st & 0xF0) == 0x90 && d[1] > 0, button = (st & 0x0F) == 0;
		pthread_mutex_lock(&sw_mtx);
		int r = combo_event(&sw_combo, d[0], down, button, now_ms());
		pthread_mutex_unlock(&sw_mtx);
		if (r)
			sw_action(r);
	}
}

/* MPC's own writes to the Private port: remember the MENU LED value */
static void sw_watch_led(const unsigned char *b, ssize_t n)
{
	for (ssize_t i = 0; i + 2 < n; i++)
		if (b[i] == 0xB0 && b[i + 1] == L3_MENU && b[i + 2] < 0x80)
			sw_menu_led = b[i + 2];
}

int snd_rawmidi_open(snd_rawmidi_t **in, snd_rawmidi_t **out, const char *name, int mode)
{
	static int (*real)(snd_rawmidi_t **, snd_rawmidi_t **, const char *, int);
	if (!real)
		real = next_sym("snd_rawmidi_open");
	int r = real(in, out, name, mode);
	if (r < 0)
		return r;
	b_start();                                  /* version B: the Force Link port (b.conf) */
	sw_start();
	if (!read_host()) {
		if (sw_ready && port_kind(out && *out ? *out : *in) == 1) {
			if (in && *in) sw_in = *in;
			if (out && *out) sw_out = *out;
		}
		if (tap_on()) {
			int k = port_kind(out && *out ? *out : *in);
			if (k >= 0) {
				if (in && *in) tap_add(*in, k);
				if (out && *out) tap_add(*out, k);
				note(k ? "tapping Private" : "tapping Public", name);
			}
		}
		return r;
	}
	int kind = port_kind(out && *out ? *out : *in);
	if (kind < 0)
		return r;
	int fd = ensure_relay(kind);
	if (fd < 0)
		return r;
	if (in && *in) {
		start_native_reader(*in, kind);   /* the Live III's own controller keeps talking: log it */
		*in = (snd_rawmidi_t *)wrap(*in, fd, 1);
	}
	if (out && *out) {
		pthread_mutex_lock(&nat_mtx);
		nat_out[kind] = *out;             /* the Live III's own controller, for the bridge */
		pthread_mutex_unlock(&nat_mtx);
		*out = (snd_rawmidi_t *)wrap(*out, fd, 2);
	}
	note(kind ? "wrapped Private" : "wrapped Public", name);
	bridge_start();
	return r;
}

int snd_rawmidi_close(snd_rawmidi_t *h)
{
	static int (*real)(snd_rawmidi_t *);
	if (!real)
		real = next_sym("snd_rawmidi_close");
	if (h == sw_in) sw_in = NULL;
	if (h == sw_out) sw_out = NULL;
	fake_t *f = as_fake(h);
	if (!f)
		return real(h);
	pthread_mutex_lock(&mtx);
	for (int i = 0; i < 16; i++)
		if (fakes[i] == f) fakes[i] = NULL;
	pthread_mutex_unlock(&mtx);
	pthread_mutex_lock(&nat_mtx);          /* the bridge stops using this handle before it goes */
	for (int k = 0; k < 2; k++) {
		if (nat_in[k] == f->real) nat_in[k] = NULL;
		if (nat_out[k] == f->real) nat_out[k] = NULL;
	}
	int r = real(f->real);
	pthread_mutex_unlock(&nat_mtx);
	free(f);
	return r;
}

ssize_t snd_rawmidi_read(snd_rawmidi_t *h, void *buf, size_t n)
{
	static ssize_t (*real)(snd_rawmidi_t *, void *, size_t);
	if (!real)
		real = next_sym("snd_rawmidi_read");
	fake_t *f = as_fake(h);
	if (!f) {
		ssize_t r = real(h, buf, n);
		int k;
		if (r > 0 && h == sw_in)
			sw_feed(buf, r);
		if (r > 0 && tap_on() && (k = tapped_as(h)) >= 0)
			tlog(k, "ctl>MPC", buf, r);
		return r;
	}
	ssize_t r = recv(f->fd, buf, n, MSG_DONTWAIT);
	return r < 0 ? -(errno ? errno : EAGAIN) : r;
}

ssize_t snd_rawmidi_write(snd_rawmidi_t *h, const void *buf, size_t n)
{
	static ssize_t (*real)(snd_rawmidi_t *, const void *, size_t);
	if (!real)
		real = next_sym("snd_rawmidi_write");
	fake_t *f = as_fake(h);
	if (!f) {
		ssize_t r = real(h, buf, n);
		int k;
		if (r > 0 && h == sw_out)
			sw_watch_led(buf, r);
		if (r > 0 && tap_on() && (k = tapped_as(h)) >= 0)
			tlog(k, "MPC>ctl", buf, r);
		return r;
	}
	ssize_t r = send(f->fd, buf, n, MSG_DONTWAIT | MSG_NOSIGNAL);
	return r < 0 ? -(errno ? errno : EAGAIN) : r;
}

int snd_rawmidi_drain(snd_rawmidi_t *h)
{
	static int (*real)(snd_rawmidi_t *);
	if (!real)
		real = next_sym("snd_rawmidi_drain");
	return as_fake(h) ? 0 : real(h);
}

int snd_rawmidi_drop(snd_rawmidi_t *h)
{
	static int (*real)(snd_rawmidi_t *);
	if (!real)
		real = next_sym("snd_rawmidi_drop");
	return as_fake(h) ? 0 : real(h);
}

int snd_rawmidi_poll_descriptors_count(snd_rawmidi_t *h)
{
	static int (*real)(snd_rawmidi_t *);
	if (!real)
		real = next_sym("snd_rawmidi_poll_descriptors_count");
	return as_fake(h) ? 1 : real(h);
}

int snd_rawmidi_poll_descriptors(snd_rawmidi_t *h, struct pollfd *p, unsigned int space)
{
	static int (*real)(snd_rawmidi_t *, struct pollfd *, unsigned int);
	if (!real)
		real = next_sym("snd_rawmidi_poll_descriptors");
	fake_t *f = as_fake(h);
	if (!f)
		return real(h, p, space);
	if (space < 1)
		return 0;
	p[0].fd = f->fd;
	p[0].events = f->dir == 1 ? POLLIN : POLLOUT;
	p[0].revents = 0;
	return 1;
}

int snd_rawmidi_poll_descriptors_revents(snd_rawmidi_t *h, struct pollfd *p, unsigned int n, unsigned short *rev)
{
	static int (*real)(snd_rawmidi_t *, struct pollfd *, unsigned int, unsigned short *);
	if (!real)
		real = next_sym("snd_rawmidi_poll_descriptors_revents");
	fake_t *f = as_fake(h);
	if (!f)
		return real(h, p, n, rev);
	*rev = (unsigned short)p[0].revents;
	return 0;
}

int snd_rawmidi_status(snd_rawmidi_t *h, void *st)
{
	static int (*real)(snd_rawmidi_t *, void *);
	if (!real)
		real = next_sym("snd_rawmidi_status");
	fake_t *f = as_fake(h);
	if (!f)
		return real(h, st);
	int r = real(f->real, st);
	pthread_mutex_lock(&mtx);
	stat_ptr[stat_n % 8] = st; stat_fd[stat_n % 8] = f->fd; stat_n++;
	pthread_mutex_unlock(&mtx);
	return r;
}

size_t snd_rawmidi_status_get_avail(const void *st)
{
	static size_t (*real)(const void *);
	if (!real)
		real = next_sym("snd_rawmidi_status_get_avail");
	int fd = -1;
	pthread_mutex_lock(&mtx);
	for (int i = 0; i < 8; i++)
		if (stat_ptr[i] == st) fd = stat_fd[i];
	pthread_mutex_unlock(&mtx);
	if (fd >= 0) {
		int n = 0;
		ioctl(fd, FIONREAD, &n);
		return (size_t)n;
	}
	return real(st);
}

#define REAL_OF(h) ({ fake_t *f_ = as_fake(h); f_ ? f_->real : (h); })
int snd_rawmidi_info(snd_rawmidi_t *h, void *i)
{ static int (*real)(snd_rawmidi_t *, void *); if (!real) real = next_sym("snd_rawmidi_info"); return real(REAL_OF(h), i); }
int snd_rawmidi_params(snd_rawmidi_t *h, void *p)
{ static int (*real)(snd_rawmidi_t *, void *); if (!real) real = next_sym("snd_rawmidi_params"); return real(REAL_OF(h), p); }
int snd_rawmidi_params_current(snd_rawmidi_t *h, void *p)
{ static int (*real)(snd_rawmidi_t *, void *); if (!real) real = next_sym("snd_rawmidi_params_current"); return real(REAL_OF(h), p); }
const char *snd_rawmidi_name(snd_rawmidi_t *h)
{ static const char *(*real)(snd_rawmidi_t *); if (!real) real = next_sym("snd_rawmidi_name"); return real(REAL_OF(h)); }
int snd_rawmidi_stream(snd_rawmidi_t *h)
{ static int (*real)(snd_rawmidi_t *); if (!real) real = next_sym("snd_rawmidi_stream"); return real(REAL_OF(h)); }
