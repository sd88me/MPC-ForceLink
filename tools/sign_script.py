#!/usr/bin/env python3
"""Compute the MPC 3.10 device-script checksum for a .lua file.

checksum = sha256(lua_bytes + salt), where salt is a 36-char string stored XOR-obfuscated
in /usr/bin/MPC (offset per build, below). The salt is read from your own MPC binary at run time
and never written anywhere, so do not commit it or paste it publicly.

usage: sign_script.py <MPC binary> <script.lua> [script.ini]   (with .ini: rewrites checksum=)
"""
import hashlib, re, sys

SALT_LEN, SALT_KEY = 0x25, 0x5F694973D3C78945
SALT_OFF = {                       # file offset of the obfuscated salt, by build id in the binary
    b'be01c5a73a5': 0x5295D30,     # 3.10.0.23 (init code at 0xc9e148)
    b'2209920bc0a': 0x5287C10,     # 3.10.0.49 (official Gen2 update; verified against the shipped MPK mini IV checksum)
}

def salt_from(mpc_path):
    data = open(mpc_path, 'rb').read()
    off = next((o for build, o in SALT_OFF.items() if build in data), None)
    if off is None:
        sys.exit('unknown MPC build: add its salt offset to SALT_OFF')
    enc = data[off:off + SALT_LEN]
    dec = bytes(b ^ ((SALT_KEY >> (8 * (i % 8))) & 0xFF) for i, b in enumerate(enc))
    return dec.split(b'\0')[0]

def main():
    mpc, lua = sys.argv[1], sys.argv[2]
    digest = hashlib.sha256(open(lua, 'rb').read() + salt_from(mpc)).hexdigest()
    if len(sys.argv) > 3:
        ini = sys.argv[3]; text = open(ini).read()
        text, n = re.subn(r'(?m)^checksum=.*$', 'checksum=' + digest, text)
        if not n: text = text.rstrip('\n') + '\nchecksum=' + digest + '\n'
        open(ini, 'w').write(text)
    print(digest)

if __name__ == '__main__':
    main()
