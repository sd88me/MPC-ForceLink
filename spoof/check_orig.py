#!/usr/bin/env python3
"""Check that stepsnap.c's expected instruction words (ORIG) are the ones at 0x2f939a0 in an MPC binary.
usage: check_orig.py <MPC binary>"""
import re, struct, sys
src = open(__file__.replace('check_orig.py', 'stepsnap.c')).read()
words = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{8})', src[src.index('static const uint32_t ORIG'):src.index('};', src.index('static const uint32_t ORIG'))])]
d = open(sys.argv[1], 'rb').read()
real = list(struct.unpack('<%dI' % len(words), d[0x2f939a0:0x2f939a0 + 4 * len(words)]))
site = struct.unpack('<I', d[0x2f939bc:0x2f939c0])[0]
print(len(words), 'words;', 'match' if words == real else 'MISMATCH', f'; word at the patch site 0x2f939bc = {site:08x}')
sys.exit(0 if words == real and site == words[7] else 1)
