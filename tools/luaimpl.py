# Resolve the C++ member function behind a sol2 mapping binding registered as name -> GOT-loaded member ptr.
# usage: luaimpl.py ELF NAME  (finds 'adrp+add NAME', then the next 'adrp xN; ldr xN,[xN,#off]' GOT load)
import sys, subprocess
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', 'local', 'pylib'))
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
p, name = sys.argv[1], sys.argv[2]
d = open(p, 'rb').read(); e = ELFFile(open(p, 'rb'))
rel = {}
for s in e.iter_sections():
    if s.header.sh_type == 'SHT_RELA':
        for r in s.iter_relocations(): rel[r['r_offset']] = r['r_addend']
sa = d.find(b'\0' + name.encode() + b'\0') + 1
out = subprocess.run([sys.executable, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), 'xref.py'), p, hex(sa)], capture_output=True, text=True).stdout.split()
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
for site in out[::3]:
    a = int(site, 16); regs = {}
    for i in md.disasm(d[a:a + 0x200], a):
        if i.mnemonic == 'adrp': r, v = i.op_str.split(', '); regs[r] = int(v[1:], 16)
        elif i.mnemonic == 'ldr' and '[' in i.op_str and '#' in i.op_str:
            r, rest = i.op_str.split(', ', 1); base = rest.strip('[]').split(',')[0]
            if base in regs and i.address > a + 8:
                got = regs[base] + int(rest.split('#')[1].rstrip(']'), 16)
                if got in rel: print(name, 'site', site, 'impl', hex(rel[got])); break
