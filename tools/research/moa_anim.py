"""MoA PA entry 0: joint table + 57 animations; poses pieces for a quick picture.
Usage: moa_anim.py <PA dir> <anim> <frame> <out.png> <core> <legs> <head> <arms> <booster> <hand> <backL> <backR>
(part game indices; see docs/research/gen1-models.md). Reads your own import folder."""
import struct,sys,math
import numpy as np

def records(d):
    h=struct.unpack_from('<7I',d,0); base=h[4]; end=h[5]
    offs=struct.unpack_from('<57I',d,base); starts=sorted(set(x for x in offs if x)); out={}
    for k,x in enumerate(offs):
        if not x and k: out[k]=None; continue
        nxt=[s for s in starts if s>x]; out[k]=(base+x, base+nxt[0] if nxt else end)
    return h,out

def parse_rec(d,o,end):
    last,count,_=struct.unpack_from('<3h',d,o); p=o+6
    durs=list(struct.unpack_from('<%dh'%(count-1),d,p)) if count>1 else []; p+=2*max(count-1,0)
    frames=[]
    for _ in range(count):
        n=struct.unpack_from('<h',d,p)[0]; p+=2; nodes=[]
        for _ in range(n):
            desc,=struct.unpack_from('<H',d,p); p+=2; fl=desc&0xff; rot=pos=None
            rm=(fl>>2)&3; pm=(fl>>6)&3
            if rm==1: rot=struct.unpack_from('<3h',d,p); p+=6
            elif rm==2: rot=struct.unpack_from('<3b',d,p); p+=4
            if pm==1: pos=struct.unpack_from('<3h',d,p); p+=6
            elif pm==2: pos=struct.unpack_from('<3b',d,p); p+=4
            nodes.append((desc>>8,fl,rot,pos))
        frames.append(nodes)
    assert 0<=end-p<=4, 'record length'
    return dict(last=last,count=count,durs=durs,frames=frames)

def joint_table(d,h):
    jt=d[h[3]:h[4]]; p=2; order=[]; slot={}; parent={}
    while jt[p]==8: order.append(jt[p+1]); p+=2
    while p+4<=len(jt) and jt[p] in (2,3):
        a,b=jt[p+1],jt[p+2]|jt[p+3]<<8
        if jt[p]==2: slot[a]=b
        else: parent[a]=None if b==0xffff else b
        p+=4
    return order,slot,parent

def state(rec,frame):
    st={}
    for f in rec['frames'][:frame+1]:
        for node,fl,r,p in f:
            o=st.setdefault(node,[None,None]); rm=(fl>>2)&3; pm=(fl>>6)&3
            if rm==3: o[0]=None
            elif rm: o[0]=r
            if pm==3: o[1]=None
            elif pm: o[1]=p
    return st

def rot(r):   # 4096 = full turn; M = Rx*Ry*Rz (confirmed, see the notes)
    if r is None: return np.eye(3)
    a,b,c=[x*2*math.pi/4096 for x in r]; ca,sa,cb,sb,cc,sc=math.cos(a),math.sin(a),math.cos(b),math.sin(b),math.cos(c),math.sin(c)
    return np.array([[1,0,0],[0,ca,-sa],[0,sa,ca]])@np.array([[cb,0,sb],[0,1,0],[-sb,0,cb]])@np.array([[cc,-sc,0],[sc,cc,0],[0,0,1]])

def world(parent,st):
    W={}
    def get(n):
        if n not in W:
            r,p=st.get(n,[None,None]); L=np.eye(4); L[:3,:3]=rot(r)
            if p: L[:3,3]=p
            par=parent.get(n); W[n]=(get(par) if par is not None else np.eye(4))@L
        return W[n]
    for n in parent: get(n)
    return W

def slot_pieces(parts):
    """parts: dict name -> piece count. Slots from the highest down hold these pieces."""
    seq=[('head',0),('core',0),('legs',0)]+[('arms',i) for i in range(parts['arms'])]
    if parts.get('hand'): seq.append(('hand',0))
    rest=list(range(1,parts['legs'])); front=rest if len(rest)<8 else rest[:4]   # four legs: 4 before, 4 after
    seq+=[('legs',i) for i in front]
    seq+=[('backR',0),('backL',0),('backL',1),('booster',0),('booster',1),('backR',1)]
    seq+=[('legs',i) for i in rest[len(front):]]
    return seq

if __name__=='__main__':
    sys.path.insert(0,__file__.rsplit('/',1)[0]); from moa_model import parse
    from PIL import Image,ImageDraw
    pa,a,fr,out=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),sys.argv[4]
    core,legs,head,arms,boost,hand,bl,br=[int(x) for x in sys.argv[5:13]]
    E={'core':2+core,'legs':15+legs,'head':57+head,'arms':127+arms,'booster':81+boost,'hand':99+hand,'backL':155+bl,'backR':215+br}
    d=open(pa+'/000.bin','rb').read(); h,recs=records(d); order,slot,parent=joint_table(d,h)
    W=world(parent,state(parse_rec(d,*recs[a]),fr))
    if hand==0: E.pop('hand')
    meshes={k:parse(open(pa+'/%03d.bin'%e,'rb').read()) for k,e in E.items()}
    if meshes['arms']['parts']==8: meshes.pop('hand',None)   # weapon arms: no hand slot
    counts={k:m['parts'] for k,m in meshes.items()}
    seq=slot_pieces(counts); n=len(slot); tris=[]
    for node,k in slot.items():
        i=n-1-k
        if i>=len(seq): continue
        part,piece=seq[i]; m=meshes.get(part)
        if not m or piece>=m['parts']: continue
        o=m['objs'][piece]; V=[(W[node]@np.array([x,y,z,1.0]))[:3]*(1,-1,1) for x,y,z in o['verts']]
        for p in o['prims']:
            vs=[V[q] for q in p[2] if q<len(V)]
            tris+=[(vs[0],vs[1],vs[2]),(vs[1],vs[3],vs[2])] if len(vs)==4 else [tuple(vs)] if len(vs)==3 else []
    S=480; img=Image.new('RGB',(S*2,S),(30,34,40)); dr=ImageDraw.Draw(img)
    for ya,ox in ((0.6,0),(2.17,S)):
        c,s=math.cos(ya),math.sin(ya); T=[]
        for t in tris:
            q=[(v[0]*c+v[2]*s,v[1],-v[0]*s+v[2]*c) for v in t]; nrm=np.cross(np.subtract(q[1],q[0]),np.subtract(q[2],q[0]))
            g=int(200*(0.35+0.65*abs(nrm[2])/(np.linalg.norm(nrm)+1e-9)))
            T.append((sum(x[2] for x in q),[(ox+S/2+x[0]*0.35,S/2-(x[1]-450)*0.35) for x in q],g))
        for _,pts,g in sorted(T,key=lambda t:-t[0]): dr.polygon(pts,fill=(g,g,g),outline=(20,20,20))
    img.save(out)
