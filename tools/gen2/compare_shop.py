"""Compare AC2 vs Another Age shop catalogues (F block from split_bin.py).
Usage: python compare_shop.py AC2_F_DIR AA_F_DIR"""
import struct, glob, os, sys
def recs(d):
    out={}; cat=0; prev=-1
    for p in sorted(glob.glob(d+'/*.bin')):
        i=int(os.path.basename(p)[:5]); b=open(p,'rb').read()
        if b[:4]!=b'JOIN': continue
        idx=struct.unpack_from('<I',b,0x10)[0]
        if idx!=prev+1: cat+=1
        prev=idx
        for o in struct.unpack_from('<8I',b,0x34):
            if o!=0xffffffff and o+0x90<=len(b) and all(32<=c<127 for c in b[o:o+18]):
                r=b[o:o+0x90]; out.setdefault(r[:18].decode().strip(),[]).append((i,cat,r)); break
    return out
a = recs(sys.argv[1]); b = recs(sys.argv[2])
new=[n for n in b if n not in a]; gone=[n for n in a if n not in b]
print('AC2',sum(map(len,a.values())),'AA',sum(map(len,b.values())))
print('NEW in AA',len(new),[(n,b[n][0][0]) for n in new])
print('GONE',gone)
ch=[]
for n in a:
    if n in b and a[n][0][2][36:]!=b[n][0][2][36:]:
        x,y=a[n][0][2],b[n][0][2]
        diffs=[(k, struct.unpack_from('<H',x,k)[0], struct.unpack_from('<H',y,k)[0]) for k in range(36,0x90,2) if x[k:k+2]!=y[k:k+2] and not (40<=k<0x72)]
        pr=(struct.unpack_from('<I',x,36)[0],struct.unpack_from('<I',y,36)[0])
        ch.append((n,pr if pr[0]!=pr[1] else None,diffs))
print('CHANGED',len(ch))
for c in ch: print(c)
