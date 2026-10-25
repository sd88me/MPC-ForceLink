// Offline test of the setup switch protocol over loopback: gcc -Wall -o t flswitch_test.c flswitch.c -lpthread && ./t
#include "flswitch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int fails;
#define CHECK(c, m) do { int ok_ = (c); printf("%-48s %s\n", m, ok_ ? "ok" : "FAIL"); if (!ok_) fails++; } while (0)

int main(void) {
    char dir[] = "/tmp/flswXXXXXX"; if (!mkdtemp(dir)) return 1;
    char conf[300], runner[300], out[300], line[400];
    snprintf(conf, sizeof conf, "%s/switch.conf", dir); snprintf(runner, sizeof runner, "%s/runner", dir);
    snprintf(out, sizeof out, "%s/ran", dir);
    FILE *f = fopen(runner, "w"); fprintf(f, "#!/bin/sh\necho \"$@\" > %s\n", out); fclose(f);
    chmod(runner, 0755); setenv("FLSW_RUNNER", runner, 1);
    f = fopen(conf, "w"); fprintf(f, "token=0123456789abcdef0123\npeer=127.0.0.1\nscript=/x/link.sh\n"); fclose(f);
    flsw_t s;
    CHECK(flsw_load(&s, conf) == 0 && !strcmp(s.peer, "127.0.0.1"), "config loaded");
    CHECK(flsw_parse(&s, "FLSW 0123456789abcdef0123 link\n") != NULL, "valid request parsed");
    CHECK(flsw_parse(&s, "FLSW wrongtoken b\n") == NULL, "wrong token refused");
    CHECK(flsw_parse(&s, "FLSW 0123456789abcdef0123 a\n") == NULL, "unknown mode refused");
    CHECK(flsw_parse(&s, "FLSW 0123456789abcdef0123 toggle\n") != NULL, "toggle accepted");
    flsw_listen(&s); usleep(200000);
    CHECK(flsw_tell_peer(&s, "link") == 0, "peer accepted over TCP");
    usleep(500000);
    f = fopen(out, "r"); line[0] = 0; if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
    CHECK(strstr(line, "--unit=forcelink-switch-") && strstr(line, "--no-block") && strstr(line, "KillMode=process") && strstr(line, "/x/link.sh switch link"), "link.sh link started via the runner");
    flsw_t bad = s; strcpy(bad.token, "ffffffffffffffffffff");
    CHECK(flsw_tell_peer(&bad, "stock") != 0, "wrong token: peer refuses");
    puts(fails ? "FAILURES" : "ALL OK");
    return fails != 0;
}
