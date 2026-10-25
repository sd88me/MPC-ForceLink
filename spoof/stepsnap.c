/* See stepsnap.h. Offsets are MPC 3.10.0.49's (FINDINGS 27); every read of MPC's memory goes through
 * process_vm_readv, so a wrong guess fails with EFAULT instead of crashing MPC. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "stepsnap.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#define VERSION 1
#define SITE 0x2f939bc   /* cbz x0, <error path> in events.GetSize(), right after the list lookup returns */
#define MAXEV 16384
#define MAXVIEW 64

/* MPC's events.GetSize(), words 0x2f939a0 .. 0x2f939d0: the patch is only made if the code is exactly this. Words 0-6
 * are the lookup of the list, word 7 the `cbz x0, <error>`, word 8 the count read, word 12 the error path. */
static const uint32_t ORIG[13] = {0xd503233f, 0xa9be7bfd, 0x910003fd, 0xf9404c00, 0xf9400001, 0xf9400821, 0xd63f0020,
				  0xb40000a0, 0xf9404400, 0xa8c27bfd, 0xd50323bf, 0xd65f03c0, 0xd2800200};

static ss_host_t host;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static struct { int note; int64_t start; int step, nsteps; } view = {-1, 0, 240, 32};
static uint8_t last_reply[MAXVIEW + 16], last_hist[96 + 8];
static size_t last_reply_n, last_hist_n;
static struct timespec last_sent;
static unsigned seq;
static int direct_reads, read_fails;
static uintptr_t logged_list[4];
static ss_ev_t *evbuf;
static struct { int active, note; int64_t tick; int len; } req;         /* a pending "06" request */
typedef struct { char *p; size_t len; union { size_t cap; char buf[16]; } u; } cxxstr;   /* a std::string, by value */
typedef cxxstr (*enc_fn)(const void *event);                           /* MPC's event -> string (0x2f9dda0) */
static enc_fn encoder;
static const uint32_t ENC_WORDS[8] = {0xd503233f, 0xd10243ff, 0xd0027221, 0xf9467821, 0xa9057bfd, 0x910143fd, 0xa90653f3, 0xaa0803f3};

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	char b[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	if (host.log)
		host.log(b);
}

static int pipefd[2] = {-1, -1};

/* A fault-safe read: process_vm_readv, else (no such syscall, as under some emulators) write() the bytes into a pipe,
 * which fails with EFAULT on a bad address instead of raising SIGSEGV. */
static int rd(const void *addr, void *buf, size_t n)
{
	if (!direct_reads) {
		struct iovec l = {buf, n}, r = {(void *)addr, n};
		ssize_t k = process_vm_readv(getpid(), &l, 1, &r, 1, 0);
		if (k == (ssize_t)n)
			return 0;
		if (k >= 0 || errno != ENOSYS)
			return -1;
		direct_reads = 1;
	}
	if (pipefd[0] < 0 && pipe2(pipefd, O_NONBLOCK | O_CLOEXEC))
		return -1;
	if (write(pipefd[1], addr, n) != (ssize_t)n)
		return -1;
	return read(pipefd[0], buf, n) == (ssize_t)n ? 0 : -1;
}

/* node: _Rb_tree_node_base {color+pad 8, parent, left, right} then the event at +0x20 */
#define N_PARENT 8
#define N_LEFT 16
#define N_RIGHT 24
#define E_TIME 0x20
#define E_TYPE 0x28
#define E_VEL 0x30     /* float 0-1 */
#define E_LEN 0x48
#define E_NOTE 0x50
#define E_END 0x58

long ss_read_list(const void *list, ss_ev_t *out, size_t cap, uint64_t *total)
{
	const uint8_t *l = list;
	uint64_t count = 0, first = 0;
	if (!list || rd(l + 0x88, &count, 8) || rd(l + 0x78, &first, 8))
		return -1;
	if (total)
		*total = count;
	uintptr_t hdr = (uintptr_t)(l + 0x68), cur = (uintptr_t)first;
	size_t n = 0, steps = 0, limit = count + 8 > 100000 ? 100000 : count + 8;
	while (cur && cur != hdr && steps++ < limit) {
		uint8_t node[E_END];
		if (rd((void *)cur, node, sizeof node))
			return -1;
		if (node[E_TYPE] == 3 && n < cap) {
			float v;
			int64_t t, ln;
			memcpy(&v, node + E_VEL, 4);
			memcpy(&t, node + E_TIME, 8);
			memcpy(&ln, node + E_LEN, 8);
			int mv = (int)((v + (v > 0 ? 1 : v < 0 ? -1 : 0) / 254.0f) * 127.0f);   /* MPC's MidiVelocity */
			out[n].time = t;
			out[n].len = ln;
			out[n].node = cur;
			out[n].note = node[E_NOTE] & 0x7f;
			out[n].vel = (uint8_t)(mv < 0 ? 0 : mv > 127 ? 127 : mv);
			n++;
		}
		uintptr_t right, left, y;
		memcpy(&right, node + N_RIGHT, 8);
		if (right) {                              /* std::_Rb_tree_increment */
			cur = right;
			for (int d = 0; d < 256; d++) {
				if (rd((void *)(cur + N_LEFT), &left, 8))
					return -1;
				if (!left)
					break;
				cur = left;
			}
		} else {
			uintptr_t x = cur;
			memcpy(&y, node + N_PARENT, 8);
			for (int d = 0; y && d < 256; d++) {
				uintptr_t yr;
				if (rd((void *)(y + N_RIGHT), &yr, 8))
					return -1;
				if (x != yr)
					break;
				x = y;
				if (rd((void *)(x + N_PARENT), &y, 8))
					return -1;
			}
			uintptr_t xr;
			if (!y || rd((void *)(x + N_RIGHT), &xr, 8))
				return -1;
			cur = (xr != y) ? y : x;
		}
	}
	return (long)n;
}

static void put7(uint8_t *p, uint64_t v, int n)
{
	for (int i = n - 1; i >= 0; i--, v >>= 7)
		p[i] = v & 0x7f;
}

static uint64_t get7(const uint8_t *p, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n; i++)
		v = (v << 7) | (p[i] & 0x7f);
	return v;
}

static void emit(const uint8_t *body, size_t n)
{
	if (!host.send || n > 8192)
		return;
	uint8_t *m = malloc(n + 4);
	if (!m)
		return;
	m[0] = 0xF0; m[1] = 0x7D; m[2] = 0x53;
	memcpy(m + 3, body, n);
	m[3 + n] = 0xF7;
	host.send(m, n + 4);
	free(m);
}

/* an encoded-string request, answered here because MPC's encoder must run on MPC's own (script) thread */
static void answer_request(long n)
{
	pthread_mutex_lock(&mtx);
	int active = req.active, note = req.note, len = req.len;
	int64_t tick = req.tick;
	req.active = 0;
	pthread_mutex_unlock(&mtx);
	if (!active || !encoder)
		return;
	for (long k = 0; k < n; k++) {
		if (evbuf[k].note != note || evbuf[k].time < tick || evbuf[k].time >= tick + len)
			continue;
		cxxstr r = encoder((const void *)(evbuf[k].node + 0x20));
		const char *p = r.len <= 15 ? r.u.buf : r.p;       /* short strings live inside the object */
		size_t sl = r.len;
		if (!p || sl == 0 || sl > 6000) {
			say("stepsnap: encoder gave %zu bytes for note %d at %lld; not sent", sl, note, (long long)tick);
			return;
		}
		uint8_t *b = malloc(sl + 12);
		if (!b)
			return;
		size_t o = 0;
		b[o++] = 0x05;
		b[o++] = (uint8_t)note;
		put7(b + o, (uint64_t)tick, 5); o += 5;
		put7(b + o, sl, 3); o += 3;
		for (size_t i = 0; i < sl; i++)
			b[o++] = (uint8_t)p[i] & 0x7f;
		emit(b, o);
		free(b);
		say("stepsnap: encoded note %d at %lld: %zu chars", note, (long long)evbuf[k].time, sl);
		return;
	}
	say("stepsnap: no note %d in [%lld, +%d) to encode", note, (long long)tick, len);
}

void ss_snap(void *list)
{
	if (!evbuf || read_fails > 20)
		return;
	uint64_t total = 0;
	long n = ss_read_list(list, evbuf, MAXEV, &total);
	if (n < 0) {
		if (++read_fails == 1)
			say("stepsnap: could not read the list at %p", list);
		return;
	}
	read_fails = 0;
	for (int i = 0; i < 4; i++) {                  /* the first look at each list: say what is in it */
		if (logged_list[i] == (uintptr_t)list)
			break;
		if (!logged_list[i]) {
			logged_list[i] = (uintptr_t)list;
			say("stepsnap: list %p: %llu events, %ld notes", list, (unsigned long long)total, n);
			for (long k = 0; k < n && k < 8; k++)
				say("stepsnap:   time %lld note %d vel %d len %lld", (long long)evbuf[k].time, evbuf[k].note,
				    evbuf[k].vel, (long long)evbuf[k].len);
			break;
		}
	}
	answer_request(n);
	pthread_mutex_lock(&mtx);
	int note = view.note, step = view.step, ns = view.nsteps;
	int64_t start = view.start;
	pthread_mutex_unlock(&mtx);

	uint8_t body[MAXVIEW + 16], hist[96 + 8];
	size_t bn = 0, hn = 0;
	int counts[128] = {0};
	uint8_t vel[MAXVIEW] = {0};
	for (long k = 0; k < n; k++) {
		counts[evbuf[k].note]++;
		if (note != 127 && evbuf[k].note != note)
			continue;
		int64_t d = evbuf[k].time - start;
		if (d < 0 || step <= 0)
			continue;
		int64_t s = d / step;
		if (s < ns && !vel[s])
			vel[s] = evbuf[k].vel ? evbuf[k].vel : 1;
	}
	if (note >= 0) {
		body[bn++] = 0x02;
		put7(body + bn, seq & 0x3fff, 2); bn += 2;
		put7(body + bn, total, 3); bn += 3;
		body[bn++] = (uint8_t)ns;
		memcpy(body + bn, vel, (size_t)ns); bn += (size_t)ns;
	}
	hist[hn++] = 0x04;
	hist[hn++] = 0;
	int distinct = 0;
	int64_t maxend = 0;
	for (long k = 0; k < n; k++)
		if (evbuf[k].time + evbuf[k].len > maxend)
			maxend = evbuf[k].time + evbuf[k].len;
	for (int i = 0; i < 128 && distinct < 24; i++)
		if (counts[i]) {
			hist[hn++] = (uint8_t)i;
			put7(hist + hn, (uint64_t)counts[i], 2); hn += 2;
			distinct++;
		}
	put7(hist + hn, (uint64_t)maxend, 5); hn += 5;
	hist[1] = (uint8_t)distinct;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	int stale = (now.tv_sec - last_sent.tv_sec) >= 2;
	pthread_mutex_lock(&mtx);
	int changed = bn != last_reply_n || (bn > 3 && memcmp(body + 3, last_reply + 3, bn - 3)) || hn != last_hist_n ||
		      memcmp(hist, last_hist, hn);
	if (changed || stale) {
		if (bn) {
			seq++;
			put7(body + 1, seq & 0x3fff, 2);
		}
		memcpy(last_reply, body, bn);
		last_reply_n = bn;
		memcpy(last_hist, hist, hn);
		last_hist_n = hn;
		last_sent = now;
	}
	pthread_mutex_unlock(&mtx);
	if (changed || stale) {
		if (bn)
			emit(body, bn);
		emit(hist, hn);
	}
}

void ss_command(const uint8_t *b, size_t n)
{
	if (n >= 9 && b[0] == 0x06) {
		pthread_mutex_lock(&mtx);
		req.note = b[1] & 0x7f;
		req.tick = (int64_t)get7(b + 2, 5);
		req.len = (int)get7(b + 7, 2);
		req.active = req.len > 0;
		pthread_mutex_unlock(&mtx);
		return;
	}
	if (n >= 10 && b[0] == 0x01) {
		pthread_mutex_lock(&mtx);
		view.note = b[1] & 0x7f;
		view.start = (int64_t)get7(b + 2, 5);
		view.step = (int)get7(b + 7, 2);
		view.nsteps = b[9] > MAXVIEW ? MAXVIEW : b[9];
		last_reply_n = 0;                     /* a new view always gets an answer on the next snapshot */
		last_sent.tv_sec = 0;
		pthread_mutex_unlock(&mtx);
		uint8_t hello[2] = {0x03, VERSION};
		emit(hello, 2);
	}
}

/* ---- the patch ---------------------------------------------------------------------------------------------- */

static uint32_t enc_b(const void *from, const void *to)       /* b */
{
	long off = ((long)(uintptr_t)to - (long)(uintptr_t)from) / 4;
	return 0x14000000u | ((uint32_t)off & 0x03ffffffu);
}

static uint32_t enc_ldr_lit_x16(const void *from, const void *lit)
{
	long off = ((long)(uintptr_t)lit - (long)(uintptr_t)from) / 4;
	return 0x58000000u | (((uint32_t)off & 0x7ffffu) << 5) | 16;
}

static int near(const uint8_t *site, long *delta)
{
	long pg = sysconf(_SC_PAGESIZE);
	for (long d = 16L << 20; d <= 100L << 20; d += 1L << 20) {
		for (int sgn = -1; sgn <= 1; sgn += 2) {
			if (sgn < 0 && (uintptr_t)site < (uintptr_t)d + (1UL << 20))
				continue;                     /* would wrap below address 0 */
			uintptr_t hint = ((uintptr_t)site + (uintptr_t)(sgn * d)) & ~(uintptr_t)(pg - 1);
			void *p = mmap((void *)hint, (size_t)pg, PROT_READ | PROT_WRITE | PROT_EXEC,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
			if (p == MAP_FAILED)
				continue;
			if ((uintptr_t)p != hint) {
				munmap(p, (size_t)pg);
				continue;
			}
			*delta = (long)((uintptr_t)p - (uintptr_t)site);
			return 0;
		}
	}
	return -1;
}

static char *guard_path;

static void *clear_guard(void *arg)
{
	(void)arg;
	sleep(30);
	FILE *f = fopen(guard_path, "w");
	if (f) {
		fputs("0\n", f);
		fclose(f);
	}
	return NULL;
}

int ss_install(uintptr_t base, const ss_host_t *h, const char *guard)
{
	host = *h;
	uint8_t *fn = (uint8_t *)(base + 0x2f939a0);
	uint32_t cur[13];
	if (rd(fn, cur, sizeof cur) || memcmp(cur, ORIG, sizeof ORIG)) {
		say("stepsnap: not MPC 3.10.0.49 (code at the patch site differs); left alone");
		return -1;
	}
	/* crash guard: two starts in a row that did not survive 30 s switch the patch off */
	if (guard) {
		int tries = 0;
		FILE *f = fopen(guard, "r");
		if (f) {
			if (fscanf(f, "%d", &tries) != 1)
				tries = 0;
			fclose(f);
		}
		if (tries >= 2) {
			say("stepsnap: disabled by the crash guard (%s says %d); delete it to try again", guard, tries);
			return -2;
		}
		f = fopen(guard, "w");
		if (f) {
			fprintf(f, "%d\n", tries + 1);
			fclose(f);
		}
	}
	if (h->encoder) {
		encoder = (enc_fn)h->encoder;
	} else {
		uint32_t w[8];
		if (!rd((void *)(base + 0x2f9dda0), w, sizeof w) && !memcmp(w, ENC_WORDS, sizeof w))
			encoder = (enc_fn)(base + 0x2f9dda0);
		else
			say("stepsnap: MPC's event encoder not where expected; deleting MPC's own notes is off");
	}
	evbuf = malloc(sizeof(ss_ev_t) * MAXEV);
	if (!evbuf)
		return -3;
	long delta;
	if (near(fn, &delta)) {
		say("stepsnap: no free page within 100 MB of the patch site; left alone");
		return -4;
	}
	uint32_t *v = (uint32_t *)((uint8_t *)fn + delta);   /* the veneer, in its own page next to MPC's code */
	uint8_t *site = fn + (SITE - 0x2f939a0);
	uint32_t *lit = v + 10;
	v[0] = 0xb5000040;                                  /* cbnz x0, v[2]: a list was found */
	v[1] = enc_b(&v[1], fn + 0x30);                     /* null: the original error path (cbz's own target) */
	v[2] = 0xa9be7bfd;                                  /* stp x29, x30, [sp, #-32]! */
	v[3] = 0xf9000be0;                                  /* str x0, [sp, #16] */
	v[4] = enc_ldr_lit_x16(&v[4], lit);
	v[5] = 0xd63f0200;                                  /* blr x16 */
	v[6] = 0xf9400be0;                                  /* ldr x0, [sp, #16] */
	v[7] = 0xa8c27bfd;                                  /* ldp x29, x30, [sp], #32 */
	v[8] = enc_b(&v[8], site + 4);                      /* on with the original: ldr x0, [x0, #0x88] ... ret */
	*(void **)lit = (void *)ss_snap;
	__builtin___clear_cache((char *)v, (char *)(v + 12));
	long pg = sysconf(_SC_PAGESIZE);
	uintptr_t page = (uintptr_t)site & ~(uintptr_t)(pg - 1);
	if (mprotect((void *)page, (size_t)pg, PROT_READ | PROT_WRITE | PROT_EXEC)) {
		say("stepsnap: mprotect failed (%s); left alone", strerror(errno));
		return -5;
	}
	*(volatile uint32_t *)site = enc_b(site, v);
	__builtin___clear_cache((char *)site, (char *)site + 4);
	mprotect((void *)page, (size_t)pg, PROT_READ | PROT_EXEC);
	say("stepsnap: patched events.GetSize() at %p (veneer %p)", (void *)site, (void *)v);
	if (guard) {                                   /* still running 30 s later: the patch did no harm, clear the guard */
		pthread_t t;
		guard_path = strdup(guard);
		if (guard_path && pthread_create(&t, NULL, clear_guard, NULL) == 0)
			pthread_detach(t);
	}
	uint8_t hello[2] = {0x03, VERSION};
	emit(hello, 2);
	return 0;
}
