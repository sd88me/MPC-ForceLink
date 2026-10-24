// Command interpreter shared by the daemon and the offline preview: text commands -> controller messages.
#ifndef FLCMD_H
#define FLCMD_H
#include <stdint.h>
#include <stddef.h>
// cable 0 = pad/OLED/system sysex port, cable 1 = button LED CC port
typedef void (*fl_send_fn)(void *ctx, int cable, const uint8_t *b, size_t n);
// Returns 0 ok, 1 = quit requested, -1 = unknown/invalid command.
//   pad <n> <r> <g> <b>            pad RGB, n = row*8+col (row 0 = header row), 0-127 each
//   clip <track 1-8> <row 1-7> <empty|clip> <r> <g> <b>   slot of a track colour: dim when empty (like MPC)
//   hdrrow <track 1-8> [grey]      header-row pad (grey = MPC's 191919)
//   btn <cc> <val>                 button LED
//   qlink <1-8> <name>|<value>|<bar|-1>      generic Q-link display
//   track <1-8> <name>|<bar>       track-level display (MPC's mixer look)
//   hdr <label>|<big text>         display 16 (selected-track header)
//   bar <1-8> <name> <bar>         partial update of the level bar only (pages 2-3)
//   syx <hex...>                   raw bytes, cable 0
int fl_cmd(const char *line, fl_send_fn send, void *ctx);
// Dim colour MPC uses for an empty slot of a track: floor(c * 5 / 100), verified against 7 captured track colours.
void fl_dim(const uint8_t in[3], uint8_t out[3]);
#endif
