import sys,json,time,struct; import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); from m import *
btn=sys.argv[1]; n=int(sys.argv[2]); tag=sys.argv[3]
B=dict(select=0x1,start=0x8,up=0x10,right=0x20,down=0x40,left=0x80,l2=0x100,r2=0x200,l1=0x400,r1=0x800,tri=0x1000,cir=0x2000,x=0x4000,sq=0x8000,none=0)
b=0
for nm in btn.split('+'): b|=B[nm]
p=obj(0); slot=0x80045198
samples=[]
f0=drv.frame()
if b: drv.call({'cmd':'press','buttons':(~b)&0xffff,'frames':n})
end=f0+n+60; last=None
while True:
    f=drv.frame()
    o=drv.read(p,376); s=drv.read(slot,928)
    if o!=last: samples.append((f,o.hex(),s.hex())); last=o
    if f>end: break
json.dump(samples,open(f'./{tag}.json','w'))
print(tag,'samples',len(samples))
