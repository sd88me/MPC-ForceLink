/* forcelink switch addin for the Force (stock MPC): hold SHIFT + MENU, then MENU again, links the Force to the
 * Live III (docs/DEVICE_SWITCH.md). Loaded by MPC through LD_PRELOAD like Skipback and the button remap,
 * and chains with them (snd_rawmidi_* resolved with libc's own dlsym, RTLD_NEXT). Inert without switch.conf.
 * Build (armhf): gcc -O2 -fPIC -shared -o forcelink-switch.so flswitch_addin.c combo.c flswitch.c -ldl -lpthread */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "combo.h"
#include "flswitch.h"

#define CONF "/data/forcelink/switch.conf"
#define F_SHIFT 0x31
#define F_MENU 0x02
typedef struct snd_rawmidi snd_rawmidi_t;

static void *next_sym(const char *name) {
    static void *(*libc_dlsym)(void *, const char *);
    if (!libc_dlsym) {
        static const char *const vers[] = {"2.34", "2.4", "2.17", "2.2.5", "2.0"};
        for (size_t i = 0; i < sizeof vers / sizeof *vers && !libc_dlsym; i++) {
            char v[16] = "GLIBC_"; strcat(v, vers[i]);
            libc_dlsym = (void *(*)(void *, const char *))dlvsym(RTLD_DEFAULT, "dlsym", v);
        }
    }
    return (libc_dlsym ? libc_dlsym : dlsym)(RTLD_NEXT, name);
}

static void note(const char *a, const char *b) {
    FILE *l = fopen("/tmp/forcelink-switch.log", "a");
    if (l) { fprintf(l, "%s %s\n", a, b ? b : ""); fclose(l); }
}

static flsw_t sw;
static combo_t sw_combo;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static snd_rawmidi_t *volatile sw_in, *volatile sw_out;
static volatile int ready, armed, menu_led;

static uint32_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static int is_private(snd_rawmidi_t *h) {
    size_t (*isz)(void) = next_sym("snd_rawmidi_info_sizeof");
    int (*info)(snd_rawmidi_t *, void *) = next_sym("snd_rawmidi_info");
    const char *(*sub)(const void *) = next_sym("snd_rawmidi_info_get_subdevice_name");
    const char *(*dev)(const void *) = next_sym("snd_rawmidi_info_get_name");
    int r = 0;
    if (!isz || !info || !sub || !dev) return 0;
    void *i = calloc(1, isz());
    if (i && !info(h, i)) {
        const char *a = sub(i), *b = dev(i);
        r = (a && strstr(a, "Private")) || (b && strstr(b, "Private"));
    }
    free(i);
    return r;
}

static void led(int v) {
    static ssize_t (*wr)(snd_rawmidi_t *, const void *, size_t);
    if (!wr) wr = next_sym("snd_rawmidi_write");
    snd_rawmidi_t *o = sw_out;
    unsigned char m[3] = {0xB0, F_MENU, (unsigned char)v};
    if (o && wr) wr(o, m, 3);
}

static void action(int r) {
    if (r == COMBO_ARMED) { armed = 1; note("switch", "armed: press MENU to link the Force to the Live III"); }
    else if (r == COMBO_CANCEL) { armed = 0; led(menu_led); note("switch", "cancelled"); }
    else if (r == COMBO_SWITCH) {
        armed = 0; led(3);
        note("switch: to link,", flsw_tell_peer(&sw, "link") ? "the Live III did not answer" : "the Live III told");
        flsw_run_local(&sw, "link");
    }
}

static void *ticker(void *arg) {
    (void)arg;
    for (unsigned i = 0;; i++) {
        usleep(100000);
        pthread_mutex_lock(&mtx);
        int r = combo_tick(&sw_combo, now_ms());
        pthread_mutex_unlock(&mtx);
        if (r) action(r);
        if (armed && (i & 1) == 0) led((i & 2) ? 3 : 0);
    }
    return NULL;
}

static void start(void) {
    static int once;
    if (once) return;
    once = 1;
    if (flsw_load(&sw, CONF)) return;
    combo_init(&sw_combo, F_SHIFT, F_MENU);
    flsw_listen(&sw);
    pthread_t t;
    if (pthread_create(&t, NULL, ticker, NULL) == 0) pthread_detach(t);
    ready = 1;
    note("switch", "ready: hold SHIFT + MENU, then press MENU");
}

static void feed(const unsigned char *b, ssize_t n) {
    static unsigned char st, d[2];
    static int k;
    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = b[i];
        if (c >= 0xF8) continue;
        if (c & 0x80) { st = c < 0xF0 ? c : 0; k = 0; continue; }
        if ((st & 0xE0) != 0x80) continue;
        d[k++] = c;
        if (k < 2) continue;
        k = 0;
        pthread_mutex_lock(&mtx);
        int r = combo_event(&sw_combo, d[0], (st & 0xF0) == 0x90 && d[1] > 0, (st & 0x0F) == 0, now_ms());
        pthread_mutex_unlock(&mtx);
        if (r) action(r);
    }
}

int snd_rawmidi_open(snd_rawmidi_t **in, snd_rawmidi_t **out, const char *name, int mode) {
    static int (*real)(snd_rawmidi_t **, snd_rawmidi_t **, const char *, int);
    if (!real) real = next_sym("snd_rawmidi_open");
    int r = real(in, out, name, mode);
    if (r < 0) return r;
    start();
    snd_rawmidi_t *h = out && *out ? *out : (in ? *in : NULL);
    if (ready && h && is_private(h)) {
        if (in && *in) sw_in = *in;
        if (out && *out) sw_out = *out;
    }
    return r;
}

int snd_rawmidi_close(snd_rawmidi_t *h) {
    static int (*real)(snd_rawmidi_t *);
    if (!real) real = next_sym("snd_rawmidi_close");
    if (h == sw_in) sw_in = NULL;
    if (h == sw_out) sw_out = NULL;
    return real(h);
}

ssize_t snd_rawmidi_read(snd_rawmidi_t *h, void *buf, size_t n) {
    static ssize_t (*real)(snd_rawmidi_t *, void *, size_t);
    if (!real) real = next_sym("snd_rawmidi_read");
    ssize_t r = real(h, buf, n);
    if (r > 0 && h == sw_in) feed(buf, r);
    return r;
}

ssize_t snd_rawmidi_write(snd_rawmidi_t *h, const void *buf, size_t n) {
    static ssize_t (*real)(snd_rawmidi_t *, const void *, size_t);
    if (!real) real = next_sym("snd_rawmidi_write");
    if (h == sw_out) {
        const unsigned char *b = buf;
        for (size_t i = 0; i + 2 < n; i++)
            if (b[i] == 0xB0 && b[i + 1] == F_MENU && b[i + 2] < 0x80) menu_led = b[i + 2];
    }
    return real(h, buf, n);
}
