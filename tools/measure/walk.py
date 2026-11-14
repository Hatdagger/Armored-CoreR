import sys,json; import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); from m import *
btn=sys.argv[1]; n=int(sys.argv[2]); tag=sys.argv[3]
p=obj(0)
drv.call({'cmd':'wtrace_reset'}); drv.call({'cmd':'wtrace_disarm_all'})
r=drv.call({'cmd':'wtrace_arm','lo':'0x%08x'%(p+8),'hi':'0x%08x'%(p+19)})
s0=state(p); f0=drv.frame()
B=dict(select=0x1,start=0x8,up=0x10,right=0x20,down=0x40,left=0x80,l2=0x100,r2=0x200,l1=0x400,r1=0x800,tri=0x1000,cir=0x2000,x=0x4000,sq=0x8000)
b=0
for nm in btn.split('+'): b|=B[nm]
drv.call({'cmd':'press','buttons':(~b)&0xffff,'frames':n}); drv.wait(n+40)
s1=state(p)
d=drv.call({'cmd':'wtrace_dump','count':4000})
json.dump(dict(start=s0,end=s1,f0=f0,dump=d),open(f'./{tag}.json','w'))
print('start',s0); print('end',s1); print('entries',len(d.get('entries',[])), 'total', d.get('total'))
