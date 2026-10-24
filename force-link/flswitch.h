// Setup switch between devices (docs/DEVICE_SWITCH.md). Config file, one key per line:
//   token=<shared secret>   peer=<other device's IP>   script=<this device's link.sh>
// Protocol: TCP 6771, one line "FLSW <token> <stock|b|toggle>\n". The switch runs `systemd-run --no-block sh <script> switch <mode>`
// so it survives the MPC restart it causes.
#ifndef FLSWITCH_H
#define FLSWITCH_H
#define FLSW_PORT 6771

typedef struct { char token[65], peer[64], script[256]; } flsw_t;

int flsw_load(flsw_t *s, const char *path);                 // 0 ok
const char *flsw_parse(const flsw_t *s, const char *line);  // valid request -> "stock" or "b", else NULL
int flsw_run_local(const flsw_t *s, const char *mode);      // detached; 0 if started
int flsw_tell_peer(const flsw_t *s, const char *mode);      // 0 if the peer accepted
void flsw_listen(const flsw_t *s);                          // background thread serving FLSW_PORT
#endif
