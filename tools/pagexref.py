# list ADRP instructions whose page equals given page(s). usage: pagexref.py ELF page_hex...
import sys
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', 'local', 'pylib'))
import numpy as np
from elftools.elf.elffile import ELFFile
e = ELFFile(open(sys.argv[1], 'rb')); t = e.get_section_by_name('.text')
base = t['sh_addr']; data = t.data(); w = np.frombuffer(data[:len(data)//4*4], dtype='<u4')
pages = set(int(p, 16) & ~0xFFF for p in sys.argv[2:])
for i in np.nonzero((w & 0x9F000000) == 0x90000000)[0]:
    ins = int(w[i]); imm = ((((ins >> 5) & 0x7FFFF) << 2) | ((ins >> 29) & 3)) << 12
    if imm & (1 << 32): imm -= 1 << 33
    pc = base + int(i)*4
    if ((pc & ~0xFFF) + imm) in pages: print(hex(pc), hex((pc & ~0xFFF) + imm))
