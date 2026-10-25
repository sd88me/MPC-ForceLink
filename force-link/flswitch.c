#include "flswitch.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static void copy_val(char *dst, size_t cap, const char *v) {
    size_t n = strcspn(v, "\r\n");
    if (n >= cap) n = cap - 1;
    memcpy(dst, v, n); dst[n] = 0;
}

int flsw_load(flsw_t *s, const char *path) {
    memset(s, 0, sizeof *s);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[320];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "token=", 6)) copy_val(s->token, sizeof s->token, line + 6);
        else if (!strncmp(line, "peer=", 5)) copy_val(s->peer, sizeof s->peer, line + 5);
        else if (!strncmp(line, "script=", 7)) copy_val(s->script, sizeof s->script, line + 7);
    }
    fclose(f);
    return (strlen(s->token) >= 16 && s->script[0]) ? 0 : -1;
}

const char *flsw_parse(const flsw_t *s, const char *line) {
    char tok[80], mode[16];
    if (sscanf(line, "FLSW %79s %15s", tok, mode) != 2 || strcmp(tok, s->token)) return NULL;
    if (!strcmp(mode, "stock")) return "stock";
    if (!strcmp(mode, "link") || !strcmp(mode, "b")) return "link";   // "b": older name
    if (!strcmp(mode, "toggle")) return "toggle";   // the receiver switches to whichever setup it is not in
    return NULL;
}

extern char **environ;

int flsw_run_local(const flsw_t *s, const char *mode) {
    if (strcmp(mode, "stock") && strcmp(mode, "link") && strcmp(mode, "toggle")) return -1;
    char *env[64]; int k = 0;                       // the child must not load the addin again
    for (char **e = environ; *e && k < 63; e++) if (strncmp(*e, "LD_PRELOAD=", 11)) env[k++] = *e;
    env[k] = NULL;
    const char *runner = getenv("FLSW_RUNNER");     // tests only
    char unit[64];                                  // unique: a unit that started flsurf stays loaded while it runs
    snprintf(unit, sizeof unit, "--unit=forcelink-switch-%ld-%d", (long)time(NULL), (int)getpid());
    char *argv[] = {(char *)(runner ? runner : "systemd-run"), "--no-block", "--collect", unit,
                    "--property=KillMode=process",       // link.sh starts flsurf in the background: keep it
                    "/bin/sh", (char *)s->script, "switch", (char *)mode, NULL};
    pid_t p;
    if (posix_spawnp(&p, argv[0], NULL, NULL, argv, env)) return -1;
    return 0;                                       // reaped by MPC's SIGCHLD handling or init; nothing to wait for
}

int flsw_tell_peer(const flsw_t *s, const char *mode) {
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET; a.sin_port = htons(FLSW_PORT);
    if (!s->peer[0] || inet_pton(AF_INET, s->peer, &a.sin_addr) != 1) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int ok = -1;
    if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0) {
        char line[128], reply[8] = {0};
        int n = snprintf(line, sizeof line, "FLSW %s %s\n", s->token, mode);
        if (send(fd, line, n, MSG_NOSIGNAL) == n && recv(fd, reply, sizeof reply - 1, 0) > 0 && !strncmp(reply, "ok", 2))
            ok = 0;
    }
    close(fd);
    return ok;
}

static void *serve(void *arg) {
    const flsw_t *s = arg;
    int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET; a.sin_port = htons(FLSW_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (ls < 0 || bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 2)) return NULL;
    for (;;) {
        int c = accept(ls, NULL, NULL);
        if (c < 0) continue;
        struct timeval tv = {2, 0};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char line[128] = {0};
        ssize_t n = recv(c, line, sizeof line - 1, 0);
        const char *mode = n > 0 ? flsw_parse(s, line) : NULL;
        send(c, mode ? "ok\n" : "no\n", 3, MSG_NOSIGNAL);
        close(c);
        if (mode) flsw_run_local(s, mode);
    }
    return NULL;
}

void flsw_listen(const flsw_t *s) {
    pthread_t t;
    if (pthread_create(&t, NULL, serve, (void *)s) == 0) pthread_detach(t);
}
