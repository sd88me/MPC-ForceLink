# Find any code (adr, adrp+add) or 8-byte data reference to given vaddrs. usage: refs.py ELF hex...
import sys, re
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', 'local', 'pylib'))
import numpy as np
from elftools.elf.elffile import ELFFile
p = sys.argv[1]; e = ELFFile(open(p, 'rb')); t = e.get_section_by_name('.text')
b = t['sh_addr']; w = np.frombuffer(t.data()[:len(t.data())//4*4], dtype='<u4')
d = open(p, 'rb').read()
ad = np.nonzero((w & 0x9F000000) == 0x10000000)[0]
ap = np.nonzero((w & 0x9F000000) == 0x90000000)[0]
for a in sys.argv[2:]:
    T = int(a, 16)
    for i in ad:
        ins = int(w[i]); imm = ((ins >> 5) & 0x7FFFF) << 2 | ((ins >> 29) & 3)
        if imm & (1 << 20): imm -= 1 << 21
        if b + i*4 + imm == T: print(a, 'adr', hex(b + i*4))
    for i in ap:
        ins = int(w[i]); rd = ins & 31; imm = ((((ins >> 5) & 0x7FFFF) << 2) | ((ins >> 29) & 3)) << 12
        if imm & (1 << 32): imm -= 1 << 33
        pg = ((b + i*4) & ~0xFFF) + imm
        if pg != T & ~0xFFF: continue
        for j in range(1, 6):
            n = int(w[i + j])
            if (n & 0xFF800000) == 0x91000000 and ((n >> 5) & 31) == rd:
                if pg + ((n >> 10) & 0xFFF) == T: print(a, 'adrp', hex(b + i*4))
                break
    for m in re.finditer(re.escape(T.to_bytes(8, 'little')), d): print(a, 'data', hex(m.start()))
