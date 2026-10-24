/* Product-code spoof + Public/Private rawmidi ports carried over TCP to a Force running flnet (config: net.conf, the Force's address).: any open of .../inmusic,product-code returns a memfd holding "ADA2\0".
 * Test shim for running the Force control surface on a Live III. Never writes the real file. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CODE "ADA2"
#define NAME "inmusic,product-code"

static int is_code(const char *p)
{
	size_t n = p ? strlen(p) : 0, m = sizeof NAME - 1;
	return n >= m && !strcmp(p + n - m, NAME);
}

static int fake(const char *via, const char *p)
{
	int fd = memfd_create("product-code", 0);
	if (fd < 0)
		return -1;
	if (write(fd, CODE, sizeof CODE) != sizeof CODE) {
		close(fd);
		return -1;
	}
	lseek(fd, 0, SEEK_SET);
	FILE *l = fopen("/tmp/forcespoof.log", "a");
	if (l) {
		fprintf(l, "%s(%s) -> memfd %d (pid %d)\n", via, p, fd, getpid());
		fclose(l);
	}
	return fd;
}


int open(const char *p, int fl, ...)
{
	static int (*real)(const char *, int, ...);
	if (!real)
		real = dlsym(RTLD_NEXT, "open");
	if (is_code(p)) {
		int fd = fake("open", p);
		if (fd >= 0)
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
	if (is_code(p)) {
		int fd = fake("open64", p);
		if (fd >= 0)
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
	if (is_code(p)) {
		int fd = fake("openat", p);
		if (fd >= 0)
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
	if (is_code(p)) {
		int fd = fake("openat64", p);
		if (fd >= 0)
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

