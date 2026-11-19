"""Master of Arena stage file (FDAT 5s+4) reader: chunks, area records, the
area models' TMD. Notes: docs/research/gen1-stages.md. Reads a file from your
own import (unpacked/GG/COM/FDAT/NNN.bin, NNN = 5 x stage + 4).

Usage: python3 moa_stage.py <import>/unpacked/GG/COM/FDAT/139.bin   (Green Silo)
"""
import struct,sys
def chunks(d):
    o=0;ch=[]
    while o+4<=len(d):
        n=struct.unpack_from('<I',d,o)[0]
        if n>len(d)-o-4: break
        ch.append(d[o+4:o+4+n]); o+=4+n
    return ch
def tmd(d, base):
    """Standard PS1 TMD at d[base]: id, flags, nobj; objects 28 bytes, offsets from base+12."""
    _,_,nobj=struct.unpack_from('<3I',d,base)
    ob=base+12; objs=[]
    for i in range(nobj):
        vt,nv,nt,nn,pt,np,sc=struct.unpack_from('<7I',d,ob+i*28)
        verts=[struct.unpack_from('<3h',d,ob+vt+k*8) for k in range(nv&0xffff)]
        prims=[]; p=ob+pt
        for k in range(np&0xffff):
            h=struct.unpack_from('<I',d,p)[0]; ilen=(h>>8)&0xff; flag=(h>>16)&0xff; mode=h>>24
            hw=struct.unpack_from('<%dH'%(ilen*2),d,p+4)
            n=4 if mode&8 else 3; tex=mode&4; gour=mode&0x10; lit=not(flag&1)
            i0=0; uv=None; col=None
            if tex: uv=[(hw[2*j]&0xff,hw[2*j]>>8) for j in range(n)]; tpage=hw[3]; cba=hw[1]; i0=2*n
            else: tpage=cba=None
            if not lit:
                nc = n if gour else 1
                col=[(hw[i0+2*j]&0xff,hw[i0+2*j]>>8,hw[i0+2*j+1]&0xff) for j in range(nc)]; i0+=2*nc
                vs=list(hw[i0:i0+n])
            else:
                if not tex: col=[(hw[0]&0xff,hw[0]>>8,hw[1]&0xff)]; i0=2
                if gour: vs=[hw[i0+2*j+1] for j in range(n)]
                else: vs=[hw[i0+1]]+list(hw[i0+2:i0+2+n-1])
            prims.append(dict(mode=mode,flag=flag,v=vs,uv=uv,col=col,tpage=tpage,cba=cba))
            p+=4+ilen*4
        objs.append(dict(verts=verts,prims=prims))
    return objs
def areas(c):
    out=[]
    for r in range(len(c)//52 if len(c)<0x6800 else 512):
        v=struct.unpack_from('<26h',c,r*52)
        if not any(v): continue
        out.append(dict(i=r,min=v[0:3],max=v[4:7],centre=v[8:11],turn=v[11]&0xffff,model=v[15],
                        nb=v[19],raw=v))
    return out
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read(); ch=chunks(d)
    objs=tmd(ch[4],0); ar=areas(ch[8])
    print('objects',len(objs),'areas',len(ar))
    for i,o in enumerate(objs):
        vs=o['verts']; bad=sum(1 for p in o['prims'] for v in p['v'] if v>=len(vs))
        rng=[(min(v[k] for v in vs),max(v[k] for v in vs)) for k in range(3)] if vs else None
        print(i,len(vs),len(o['prims']),'bad',bad,rng, sorted(set((hex(p['mode']),p['flag']) for p in o['prims'])))
