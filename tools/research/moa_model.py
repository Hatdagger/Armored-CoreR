"""MoA PA model entries: {size, 0, 0xC, parts, lods=3, nobj} + TMD object table at 0x18."""
import struct,sys,collections
def parse(d):
    size,_,c,parts,lods,nobj=struct.unpack_from('<6I',d,0)
    base=0x18; objs=[]
    for i in range(nobj):
        vt,nv,nt,nn,pt,np,sc=struct.unpack_from('<7I',d,base+i*28)
        verts=[struct.unpack_from('<3h',d,base+vt+k*8) for k in range(nv&0xffff)]
        norms=[struct.unpack_from('<3h',d,base+nt+k*8) for k in range(nn&0xffff)]
        prims=[]; p=base+pt; modes=collections.Counter()
        for k in range(np&0xffff):
            h=struct.unpack_from('<I',d,p)[0]; ilen=(h>>8)&0xff; flag=(h>>16)&0xff; mode=h>>24
            hw=struct.unpack_from('<%dH'%(ilen*2),d,p+4)
            quad=bool(mode&8); tex=bool(mode&4); gour=bool(mode&0x10); lit=not(flag&1)
            nvx=4 if quad else 3
            idx=hw[(4 if quad else 3)*2 if tex else 2:]  # after uv/colour words
            if lit:
                if gour: vs=[idx[2*j+1] for j in range(nvx)]
                else: vs=[idx[1]]+list(idx[2:2+nvx-1])
            else:
                vs=list(idx[-((nvx+1)//2*2):][:nvx]) if True else None
            uv=[(hw[2*j]&0xff,hw[2*j]>>8) for j in range(nvx)] if tex else None
            prims.append((mode,flag,vs,uv,(hw[1],hw[3]) if tex else None))
            modes[(hex(mode),flag,ilen)]+=1
            p+=4+ilen*4
        objs.append(dict(verts=verts,norms=norms,prims=prims,modes=modes,np_hi=np>>16,nv_hi=nv>>16,nn_hi=nn>>16,scale=sc))
    return dict(parts=parts,lods=lods,objs=objs)
if __name__=='__main__':
    m=parse(open(sys.argv[1],'rb').read())
    print('parts',m['parts'],'lods',m['lods'])
    for i,o in enumerate(m['objs']):
        vs=o['verts']; mx=max(max(v[k] for v in vs) for k in range(3)) if vs else 0
        bad=sum(1 for p in o['prims'] for v in p[2] if v>=len(vs))
        print(i,'v',len(vs),'n',len(o['norms']),'p',len(o['prims']),'hi',o['nv_hi'],o['nn_hi'],o['np_hi'],'sc %x'%o['scale'],'bad',bad,dict(o['modes']))
