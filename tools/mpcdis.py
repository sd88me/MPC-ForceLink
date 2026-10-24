# usage: dis.py ELF start_hex end_hex  -- disassemble aarch64, annotate adrp/add string refs
import sys
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', 'local', 'pylib'))
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
d = open(sys.argv[1], 'rb').read()
s, e = int(sys.argv[2], 16), int(sys.argv[3], 16)
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
def cstr(a):
    if a <= 0 or a >= len(d): return None
    end = d.find(b'\0', a, a + 120)
    if end < 0: return None
    t = d[a:end]
    if len(t) >= 3 and all(32 <= c < 127 for c in t): return t.decode()
regs = {}
for i in md.disasm(d[s:e], s):  # file offset == vaddr for .text here
    note = ''
    ops = i.op_str
    if i.mnemonic == 'adrp':
        r, v = ops.split(', '); regs[r] = int(v.replace('#', ''), 16)
    elif i.mnemonic == 'add' and ops.count(',') == 2:
        r, src, imm = [x.strip() for x in ops.split(',')]
        if src in regs and imm.startswith('#'):
            a = regs[src] + int(imm[1:], 16 if '0x' in imm else 10)
            st = cstr(a); note = f'  ; {hex(a)} "{st}"' if st else f'  ; {hex(a)}'
    print(f'{i.address:x}: {i.mnemonic:8} {ops}{note}')
