// Offline test of stepsnap on aarch64 (QEMU): a fake events.GetSize() made of MPC 3.10.0.49's own instruction words,
// a std::multiset laid out as in MPC (event at node + 0x20), then patch, call and check.
//   gcc -c stepsnap.c && g++ -O1 -Wall -Wextra -o t stepsnap_test.cpp stepsnap.o -lpthread && ./t        (aarch64)
extern "C" {
#include "stepsnap.h"
}
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
#include <unistd.h>

struct Ev {                                  // the event as MPC lays it out (offsets from the event, node + 0x20)
	alignas(8) unsigned char raw[0x40];
	Ev(long t, int type, float vel, long len, int note) {
		memset(raw, 0, sizeof raw);
		memcpy(raw + 0x00, &t, 8);
		raw[0x08] = (unsigned char)type;
		memcpy(raw + 0x10, &vel, 4);
		memcpy(raw + 0x28, &len, 8);
		raw[0x30] = (unsigned char)note;
	}
	long time() const { long t; memcpy(&t, raw, 8); return t; }
};
struct Cmp { bool operator()(const Ev &a, const Ev &b) const { return a.time() < b.time(); } };
typedef std::multiset<Ev, Cmp> Set;

static int fails;
#define CHECK(c, m) do { int ok_ = (c); printf("%-60s %s\n", m, ok_ ? "ok" : "FAIL"); if (!ok_) fails++; } while (0)

// the list as the script's wrapper finds it: wrapper + 0x98 -> object with a vtable whose slot 2 returns the list
alignas(16) static unsigned char listbuf[0x200];
static void *g_list;
static void *get_list(void *) { return g_list; }
static void *vtable[4] = {nullptr, nullptr, (void *)get_list, nullptr};
struct Src { void **vt; };
static Src src = {vtable};
struct Wrapper { unsigned char pad[0x98]; Src *s; };
static Wrapper wrapper;

// MPC's events.GetSize(): the 13 words of 0x2f939a0-0x2f939d0, then a frame pop, `autiasp` and `ret` so the "error path" returns 0x10
extern "C" unsigned long fake_get_size(void *);
__asm__(".text\n.globl fake_get_size\n.balign 16\nfake_get_size:\n"
	".word 0xd503233f,0xa9be7bfd,0x910003fd,0xf9404c00,0xf9400001,0xf9400821,0xd63f0020,0xb40000a0\n"
	".word 0xf9404400,0xa8c27bfd,0xd50323bf,0xd65f03c0,0xd2800200,0xa8c27bfd,0xd50323bf,0xd65f03c0\n");

// call it with callee-saved registers loaded with patterns; report whether they survived
extern "C" unsigned long call_checked(void *fn, void *arg, unsigned long *ok);
__asm__(".text\n.globl call_checked\n.balign 16\ncall_checked:\n"
	"stp x29,x30,[sp,#-96]!\nmov x29,sp\nstp x19,x20,[sp,#16]\nstp x21,x22,[sp,#32]\nstp x23,x24,[sp,#48]\n"
	"stp x25,x26,[sp,#64]\nstp x27,x28,[sp,#80]\nstr x2,[sp,#-16]!\n"
	"mov x16,x0\nmov x0,x1\n"
	"movz x19,#0x101\nmovz x20,#0x102\nmovz x21,#0x103\nmovz x22,#0x104\nmovz x23,#0x105\n"
	"movz x24,#0x106\nmovz x25,#0x107\nmovz x26,#0x108\nmovz x27,#0x109\nmovz x28,#0x10a\n"
	"blr x16\n"
	"mov x9,#1\n"
	"cmp x19,#0x101\ncsel x9,xzr,x9,ne\ncmp x20,#0x102\ncsel x9,xzr,x9,ne\ncmp x21,#0x103\ncsel x9,xzr,x9,ne\n"
	"cmp x22,#0x104\ncsel x9,xzr,x9,ne\ncmp x23,#0x105\ncsel x9,xzr,x9,ne\ncmp x24,#0x106\ncsel x9,xzr,x9,ne\n"
	"cmp x25,#0x107\ncsel x9,xzr,x9,ne\ncmp x26,#0x108\ncsel x9,xzr,x9,ne\ncmp x27,#0x109\ncsel x9,xzr,x9,ne\n"
	"cmp x28,#0x10a\ncsel x9,xzr,x9,ne\n"
	"ldr x2,[sp],#16\nstr x9,[x2]\n"
	"ldp x19,x20,[sp,#16]\nldp x21,x22,[sp,#32]\nldp x23,x24,[sp,#48]\nldp x25,x26,[sp,#64]\nldp x27,x28,[sp,#80]\n"
	"ldp x29,x30,[sp],#96\nret\n");

static std::vector<std::vector<unsigned char>> sent;
static void host_send(const unsigned char *m, size_t n) { sent.emplace_back(m, m + n); }
static void host_log(const char *l) { printf("   log: %s\n", l); }

static std::vector<unsigned char> latest(unsigned char op)
{
	for (size_t i = sent.size(); i-- > 0;)
		if (sent[i].size() > 4 && sent[i][3] == op)
			return sent[i];
	return {};
}

static void view(int note, long start, int step, int ns)
{
	unsigned char c[10] = {0x01, (unsigned char)note};
	for (int i = 0; i < 5; i++) c[2 + i] = (unsigned char)((start >> (7 * (4 - i))) & 0x7f);
	c[7] = (unsigned char)((step >> 7) & 0x7f);
	c[8] = (unsigned char)(step & 0x7f);
	c[9] = (unsigned char)ns;
	ss_command(c, sizeof c);
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	Set *ms = new (listbuf + 0x60) Set();
	// pad 36 on steps 0, 4, 8, 12 of bar 0 and 2 of bar 1 (240 ticks per step); pad 38 on steps 4, 12; a type-4 event
	const float v127 = 1.0f / 127.0f;
	for (int s : {0, 4, 8, 12}) ms->insert(Ev(s * 240, 3, (100 + s) * v127, 120, 36));
	ms->insert(Ev(3840 + 2 * 240, 3, 90 * v127, 120, 36));
	for (int s : {4, 12}) ms->insert(Ev(s * 240, 3, 64 * v127, 120, 38));
	ms->insert(Ev(5 * 240, 4, 77 * v127, 0, 36));         // not a note
	ms->insert(Ev(6 * 240, 3, 0.0f, 120, 40));            // velocity 0 stays visible as 1
	g_list = listbuf;
	wrapper.s = &src;
	CHECK(sizeof(Set) == 48 && (unsigned char *)&*ms->begin() - 0x20 == *(unsigned char **)(listbuf + 0x78) &&
	      *(unsigned long *)(listbuf + 0x88) == ms->size(), "harness list has MPC's layout (leftmost +0x78, count +0x88)");

	// reading directly
	ss_ev_t ev[64];
	uint64_t total = 0;
	long n = ss_read_list(listbuf, ev, 64, &total);
	CHECK(n == 8 && total == 9, "ss_read_list: 8 notes of 9 events (the type-4 one skipped)");
	bool order = true;
	for (long i = 1; i < n; i++) order = order && ev[i].time >= ev[i - 1].time;
	CHECK(order && ev[0].time == 0 && ev[0].note == 36 && ev[0].vel == 100 && ev[0].len == 120, "events in time order, fields right");

	// unpatched call
	unsigned long ok = 0, r = call_checked((void *)fake_get_size, &wrapper, &ok);
	CHECK(r == 9 && ok, "unpatched: returns the count, callee-saved registers kept");

	// wrong code at the site: left alone
	static ss_host_t host = {host_send, host_log};
	CHECK(ss_install((uintptr_t)fake_get_size - 0x2f939a0 + 64, &host, nullptr) == -1, "wrong code at the patch site: left alone");

	char guard[64];
	// crash guard
	snprintf(guard, sizeof guard, "/tmp/stepsnap_guard_%d", (int)getpid());
	FILE *f = fopen(guard, "w");
	fprintf(f, "2\n");
	fclose(f);
	CHECK(ss_install((uintptr_t)fake_get_size - 0x2f939a0, &host, guard) == -2, "crash guard at 2: patch refused");
	remove(guard);

	// install (with a guard file; it reads 1 afterwards)
	remove(guard);
	CHECK(ss_install((uintptr_t)fake_get_size - 0x2f939a0, &host, guard) == 0, "install: patched");
	CHECK(latest(0x03).size() == 6 && latest(0x03)[4] == 1, "hello sent");

	view(36, 0, 240, 16);
	sent.clear();
	r = call_checked((void *)fake_get_size, &wrapper, &ok);
	CHECK(r == 9 && ok, "patched: same count, callee-saved registers kept");
	auto rep = latest(0x02);
	CHECK(rep.size() == 4 + 2 + 3 + 1 + 16 + 0 + 0 - 0 + 0 || rep.size() == 4 + 6 + 1 + 16, "reply has the right size");
	bool right = rep.size() >= 4 + 7 + 16;
	if (right) {
		const unsigned char *vel = &rep[3 + 7];
		unsigned want[16] = {100, 0, 0, 0, 104, 0, 0, 0, 108, 0, 0, 0, 112, 0, 0, 0};
		for (int i = 0; i < 16; i++) right = right && vel[i] == want[i];
		right = right && rep[3 + 3] == 0 && rep[3 + 5] == 9 && rep[3 + 6] == 16;   // count 9, 16 steps
	}
	CHECK(right, "pad 36 bar 1: steps 0, 4, 8, 12 with their velocities");
	auto hist = latest(0x04);
	CHECK(hist.size() > 4 && hist[4] == 3, "note histogram: 3 distinct notes (36, 38, 40)");
	view(36, 3840, 240, 16);
	sent.clear();
	call_checked((void *)fake_get_size, &wrapper, &ok);
	rep = latest(0x02);
	CHECK(rep.size() >= 4 + 7 + 16 && rep[3 + 7 + 2] == 90 && rep[3 + 7 + 0] == 0, "bar 2: step 2 = 90");
	view(38, 0, 240, 16);
	sent.clear();
	call_checked((void *)fake_get_size, &wrapper, &ok);
	rep = latest(0x02);
	CHECK(rep.size() >= 4 + 7 + 16 && rep[3 + 7 + 4] == 64 && rep[3 + 7 + 12] == 64 && rep[3 + 7 + 0] == 0, "pad 38: steps 4 and 12");
	view(40, 0, 240, 16);
	sent.clear();
	call_checked((void *)fake_get_size, &wrapper, &ok);
	rep = latest(0x02);
	CHECK(rep.size() >= 4 + 7 + 16 && rep[3 + 7 + 6] == 1, "velocity 0 is shown as 1");

	// unchanged data: no new reply within 2 s
	sent.clear();
	call_checked((void *)fake_get_size, &wrapper, &ok);
	CHECK(sent.empty(), "same data again: no traffic");
	// a change goes out
	ms->insert(Ev(2 * 240, 3, 50 * v127, 120, 40));
	call_checked((void *)fake_get_size, &wrapper, &ok);
	rep = latest(0x02);
	CHECK(rep.size() >= 4 + 7 + 16 && rep[3 + 7 + 2] == 50, "a new event is sent at once");

	// no list found: the original error path (returns 0x10 here) and no crash
	g_list = nullptr;
	r = call_checked((void *)fake_get_size, &wrapper, &ok);
	CHECK(r == 0x10 && ok, "no list: original error path");
	g_list = listbuf;

	// garbage in the list: no crash
	unsigned char bad[0x200];
	memset(bad, 0, sizeof bad);
	*(unsigned long *)(bad + 0x88) = 5;
	*(unsigned long *)(bad + 0x78) = 0xdead0000UL;
	CHECK(ss_read_list(bad, ev, 64, &total) == -1, "unreadable node: -1, no crash");
	*(unsigned long *)(bad + 0x78) = (unsigned long)(bad + 0x78 + 0x100000 * 0);   // points into itself: bounded
	long nb = ss_read_list(bad, ev, 64, &total);
	CHECK(nb == -1 || nb >= 0, "self-referencing node: terminates");

	puts(fails ? "FAILURES" : "ALL OK");
	return fails != 0;
}
