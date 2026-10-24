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

static void *relay(void *arg)
{
	int kind = (int)(long)arg, fd = thr_fd[kind], tcp = -1;
	char b[1024];
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
			if (n > 0 && send(tcp, b, (size_t)n, MSG_NOSIGNAL) < 0) { close(tcp); tcp = -1; note("link lost (send)", NULL); continue; }
		}
		if (p[1].revents & (POLLIN | POLLHUP | POLLERR)) {
			ssize_t n = read(tcp, b, sizeof b);
			if (n <= 0) { close(tcp); tcp = -1; note("link lost (recv)", NULL); continue; }
			if (write(fd, b, (size_t)n) < 0) { /* MPC side full: drop */ }
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

#define FWD(ret, name, decl, call) \
	ret name decl { static ret (*real) decl; if (!real) real = next_sym(#name); return real call; }

int snd_rawmidi_open(snd_rawmidi_t **in, snd_rawmidi_t **out, const char *name, int mode)
{
	static int (*real)(snd_rawmidi_t **, snd_rawmidi_t **, const char *, int);
	if (!real)
		real = next_sym("snd_rawmidi_open");
	int r = real(in, out, name, mode);
	if (r < 0 || !read_host())
		return r;
	int kind = port_kind(out && *out ? *out : *in);
	if (kind < 0)
		return r;
	int fd = ensure_relay(kind);
	if (fd < 0)
		return r;
	if (in && *in) *in = (snd_rawmidi_t *)wrap(*in, fd, 1);
	if (out && *out) *out = (snd_rawmidi_t *)wrap(*out, fd, 2);
	note(kind ? "wrapped Private" : "wrapped Public", name);
	return r;
}

int snd_rawmidi_close(snd_rawmidi_t *h)
{
	static int (*real)(snd_rawmidi_t *);
	if (!real)
		real = next_sym("snd_rawmidi_close");
	fake_t *f = as_fake(h);
	if (!f)
		return real(h);
	pthread_mutex_lock(&mtx);
	for (int i = 0; i < 16; i++)
		if (fakes[i] == f) fakes[i] = NULL;
	pthread_mutex_unlock(&mtx);
	int r = real(f->real);
	free(f);
	return r;
}

ssize_t snd_rawmidi_read(snd_rawmidi_t *h, void *buf, size_t n)
{
	static ssize_t (*real)(snd_rawmidi_t *, void *, size_t);
	if (!real)
		real = next_sym("snd_rawmidi_read");
	fake_t *f = as_fake(h);
	if (!f)
		return real(h, buf, n);
	ssize_t r = recv(f->fd, buf, n, MSG_DONTWAIT);
	return r < 0 ? -(errno ? errno : EAGAIN) : r;
}

ssize_t snd_rawmidi_write(snd_rawmidi_t *h, const void *buf, size_t n)
{
	static ssize_t (*real)(snd_rawmidi_t *, const void *, size_t);
	if (!real)
		real = next_sym("snd_rawmidi_write");
	fake_t *f = as_fake(h);
	if (!f)
		return real(h, buf, n);
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
