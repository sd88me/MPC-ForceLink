import pickle
ev=pickle.load(open('ev.pkl','rb'))
CIN={4:3,5:1,6:2,7:3}
buf=bytearray(); syx=[]
for t,ty,xf,ep,dev,data in ev:
    if dev!=5 or xf!=3 or ep!=1 or ty!='S': continue
    for i in range(0,len(data)-3,4):
        h=data[i]; cin=h&15
        if h>>4!=0 or cin not in CIN: continue
        p=data[i+1:i+1+CIN[cin]]
        if p[0]==0xf0: buf=bytearray()
        buf+=p
        if cin in (5,6,7): syx.append((t,bytes(buf))); buf=bytearray()
disp={}
for t,p in syx:
    if p[1:5]!=bytes([0x47,0x7f,0x40,0x0b]): continue
    n=p[5]*128+p[6]; dsp,y0,y1,x0,x1=p[7:12]; data=p[12:-1]
    pages=y1-y0+1; w=x1-x0+1
    ok=len(data)==(pages*w*8+6)//7
    print(f'{t:.3f} disp={dsp} pages {y0}-{y1} x {x0}-{x1} n={n} data={len(data)} ok={ok}')
    bits=''.join(f'{b:07b}' for b in data)
    by=[int(bits[i*8:i*8+8],2) for i in range(pages*w)]
    img=disp.setdefault(dsp,[[0]*128 for _ in range(32)])
    for pi in range(pages):
        for xi in range(w):
            v=by[pi*w+xi]
            for bit in range(8): img[(y0+pi)*8+bit][x0+xi]=(v>>bit)&1
import sys
for k in sorted(disp):
    print('== display',k)
    for row in disp[k]: print(''.join('#' if v else '.' for v in row))
