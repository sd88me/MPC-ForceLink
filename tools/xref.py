# Find aarch64 ADRP+ADD/LDR xrefs to given virtual addresses. usage: xref.py ELF hexaddr...
import sys, struct
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', 'local', 'pylib'))
from elftools.elf.elffile import ELFFile
import numpy as np
f = open(sys.argv[1], 'rb'); e = ELFFile(f)
targets = set(int(a, 16) for a in sys.argv[2:])
text = e.get_section_by_name('.text')
base = text['sh_addr']; data = text.data()
w = np.frombuffer(data[:len(data)//4*4], dtype='<u4')
adrp = np.nonzero((w & 0x9F000000) == 0x90000000)[0]
for i in adrp:
    ins = int(w[i]); rd = ins & 31
    immlo = (ins >> 29) & 3; immhi = (ins >> 5) & 0x7FFFF
    imm = ((immhi << 2) | immlo) << 12
    if imm & (1 << 32): imm -= 1 << 33
    pc = base + i*4; page = (pc & ~0xFFF) + imm
    for j in range(1, 6):
        if i+j >= len(w): break
        n = int(w[i+j])
        if (n & 0xFF800000) == 0x91000000 and ((n >> 5) & 31) == rd:  # add imm
            sh = (n >> 22) & 1; im = (n >> 10) & 0xFFF
            a = page + (im << (12 if sh else 0))
            if a in targets: print(hex(pc), '->', hex(a))
            break
