import sys,subprocess,re
sys.argv=['x']
src=open("hs.py").read().split("t0=rows[0][0]")[0]
exec(src)
# handshake: OUT cab0 sysex up to the first 0A
seq=[]
for t,d,c,p in rows:
    if d!='OUT' or c!=0 or p[0]!=0xf0: continue
    if p[:5]==bytes([0xf0,0x47,0x7f,0x40,0x0a]): break
    if p[:5]==bytes([0xf0,0x47,0x00,0x40,0x52]) and seq: continue  # only first challenge
    seq.append(p)
open('fl/handshake.syx','wb').write(b''.join(seq))
print('handshake msgs',len(seq),sum(map(len,seq)),'bytes')
def pack7(b):
    bits=''.join(f'{x:08b}' for x in b); bits+='0'*((-len(bits))%7)
    return bytes(int(bits[i:i+7],2) for i in range(0,len(bits),7))
def oled(disp,img):  # img[32][128]
    cols=bytearray()
    for pg in range(4):
        for x in range(128):
            v=0
            for bit in range(8): v|=img[pg*8+bit][x]<<bit
            cols.append(v)
    pk=pack7(cols); n=5+len(pk)
    return bytes([0xf0,0x47,0x7f,0x40,0x0b,n>>7,n&127,disp,0,3,0,127])+pk+b'\xf7'
pat=bytearray()
for d in range(8): pat+=bytes([0xf0,0x47,0x7f,0x40,0x0a,0,7,d,0,3,0,0x7f,0,0,0xf7])[:0]
for d in range(8): pat+=bytes([0xf0,0x47,0x7f,0x40,0x0a,0x00,0x07,d,0x00,0x03,0x00,0x7f,0x00,0x00,0xf7])
for d in range(8):
    img=[[0]*128 for _ in range(32)]
    for x in range(128):
        img[0][x]=img[31][x]=1
    for y in range(32): img[y][0]=img[y][127]=1
    for x in range(8,8+(d+1)*14):
        for y in range(12,20): img[y][x]=1
    pat+=oled(d,img)
import colorsys
for i in range(64):
    r,g,b=colorsys.hsv_to_rgb((i%8)/8+((i//8)%2)*0.06,1,0.35+0.5*(i//8)/7)
    pat+=bytes([0xf0,0x47,0x7f,0x40,0x65,0,4,i,int(r*127),int(g*127),int(b*127),0xf7])
open('fl/pattern.syx','wb').write(bytes(pat)); print('pattern',len(pat))
open('fl/ping.syx','wb').write(bytes.fromhex('f047004052000431391640f7'))
