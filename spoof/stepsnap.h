/* stepsnap: read the note events of a clip or sequence out of MPC 3.10.0.49's memory, for the Force STEP SEQ page
 * (docs/DEVICE_SWITCH.md is unrelated; see link/HANDOFF_LINK.md, "STEP SEQ"). Scripts can only create / move / delete
 * events and ask the list's size, so the library patches the tail of `events.GetSize()` (0x2f939bc in 3.10.0.49): when
 * the script calls it, the list MPC just looked up is walked (std::multiset of events, FINDINGS 27) and the notes for
 * the view the script subscribed to go back to it as a sysex. Inert on any other build (the patch site is checked
 * word for word), and disables itself after two crashes in a row.
 *
 * Sysex (F0 7D 53 <op> ... F7), 7-bit data:
 *   script -> here  01 <note> <start:5> <step:2> <nsteps>   subscribe: note (127 = any note), first tick (5 x 7 bits),
 *                                                           ticks per step (2 x 7 bits), number of steps (<= 64)
 *   here -> script  03 <version>                            the patch is active
 *                   02 <seq:2> <count:3> <nsteps> <vel>...  per step the velocity (1-127, 0 = none) of the note's first
 *                                                           event in that step; count = events in the list
 *                   04 <n> (<note> <count:2>)...            distinct notes in the list (up to 24) */
#ifndef STEPSNAP_H
#define STEPSNAP_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
	void (*send)(const uint8_t *msg, size_t n);   /* a complete sysex for the script */
	void (*log)(const char *line);
} ss_host_t;

typedef struct {
	int64_t time, len;
	uint8_t note, vel;                            /* vel 0-127 */
} ss_ev_t;

/* Patch events.GetSize() (3.10.0.49 only). base = load bias of MPC. 0 = patched, <0 = left alone (see the log). */
int ss_install(uintptr_t base, const ss_host_t *host, const char *guard_file);
/* A command from the script: the sysex body after F0 7D 53, without F7. */
void ss_command(const uint8_t *body, size_t n);
/* The veneer calls this with the event list MPC just looked up. Also usable directly (tests). */
void ss_snap(void *list);
/* Walk a list (node layout of libstdc++'s std::multiset at list + 0x60). Returns events stored (<= cap), -1 on error;
 * *total gets the list's own count. */
long ss_read_list(const void *list, ss_ev_t *out, size_t cap, uint64_t *total);

#endif
