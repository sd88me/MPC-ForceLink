// Force controller input parser: raw MIDI bytes from the controller -> named events.
#ifndef FLPARSE_H
#define FLPARSE_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t status, d[2]; int have, need; int in_sysex; size_t sx_len; int knob_prev[8];
} fl_parser;

// cb receives one human/machine readable line (no trailing newline).
typedef void (*fl_cb)(void *ctx, const char *line);

void fl_parser_init(fl_parser *p);
void fl_parser_feed(fl_parser *p, int port, const uint8_t *b, size_t n, fl_cb cb, void *ctx);
const char *fl_button_name(int n);   // NULL if unknown
#endif
