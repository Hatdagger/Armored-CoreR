"""Assemble an AC from acr-import's exported models (OBJ + PNG + set file +
animation text) and draw it: checks the export without the disc formats.
Usage: moa_export_view.py <import>/models <set> <anim> <frame> <out.png> core=0 legs=0 head=0 arms=0 booster=0 hand=0 back-left=0 back-right=0"""
import sys,math,re,os
import numpy as np
from PIL import Image,ImageDraw

def load_set(models,sid):
    t=open('%s/sets/set-%02d.toml'%(models,sid)).read(); nodes={}
    for blk in t.split('[[node]]')[1:]:
        g=lambda k: re.search(r'^%s\s*=\s*(.+)$'%k,blk,re.M)
        n=int(g('id').group(1)); nodes[n]=dict(parent=int(g('parent').group(1)),
            draws=g('draws').group(1).strip('"') if g('draws') else None,
            pos=eval(g('position').group(1)) if g('position') else None, rot=eval(g('rotation').group(1)) if g('rotation') else None)
    return nodes

def load_anim(models,sid,a):
    txt=open('%s/sets/set-%02d-anims.txt'%(models,sid)).read().split('\nanim ')
    for blk in txt[1:]:
        lines=blk.splitlines()
        if int(lines[0].split()[0])!=a: continue
        frames=[]; cur=None
        for l in lines[1:]:
            if l.startswith('frame'): cur=[]; frames.append(cur)
            elif l.strip().startswith('node'):
                w=l.split(); n=int(w[1]); r=p='keep'
                if 'rot' in w: i=w.index('rot'); r=None if w[i+1]=='zero' else tuple(map(int,w[i+1:i+4]))
                if 'pos' in w: i=w.index('pos'); p=None if w[i+1]=='zero' else tuple(map(int,w[i+1:i+4]))
                cur.append((n,r,p))
        return frames

def R(r):
    if r is None: return np.eye(3)
    a,b,c=[x*2*math.pi/4096 for x in r]
    X=np.array([[1,0,0],[0,math.cos(a),-math.sin(a)],[0,math.sin(a),math.cos(a)]])
    Y=np.array([[math.cos(b),0,math.sin(b)],[0,1,0],[-math.sin(b),0,math.cos(b)]])
    Z=np.array([[math.cos(c),-math.sin(c),0],[math.sin(c),math.cos(c),0],[0,0,1]])
    return X@Y@Z

def load_obj(path):
    V=[];T=[];F=[];mat=None;mtl={}
    d=os.path.dirname(path)
    for l in open(path):
        w=l.split()
        if not w: continue
        if w[0]=='v': V.append(tuple(map(float,w[1:4])))
        elif w[0]=='vt': T.append(tuple(map(float,w[1:3])))
        elif w[0]=='usemtl': mat=w[1]
        elif w[0]=='f': F.append((mat,[tuple(int(x) if x else 0 for x in c.split('/')) for c in w[1:]]))
    cur=None
    for l in open(os.path.join(d,'part.mtl')):
        w=l.split()
        if not w: continue
        if w[0]=='newmtl': cur=w[1]; mtl[cur]={}
        elif w[0]=='Kd': mtl[cur]['kd']=tuple(map(float,w[1:4]))
        elif w[0]=='map_Kd': p=os.path.normpath(os.path.join(d,w[1])); mtl[cur]['tex']=Image.open(p).convert('RGB') if os.path.exists(p) else None
    return V,T,F,mtl

if __name__=='__main__':
    models,sid,a,fr,out=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),int(sys.argv[4]),sys.argv[5]
    sel=dict(x.split('=') for x in sys.argv[6:]); sel={k:int(v) for k,v in sel.items()}
    nodes=load_set(models,sid); st={n:[v['rot'],v['pos']] for n,v in nodes.items()}
    for f in (load_anim(models,sid,a) or [])[:fr+1]:
        for n,r,p in f:
            if r!='keep': st[n][0]=r
            if p!='keep': st[n][1]=p
    W={}
    def world(n):
        if n not in W:
            L=np.eye(4); L[:3,:3]=R(st[n][0])
            if st[n][1]: L[:3,3]=st[n][1]
            par=nodes[n]['parent']; W[n]=(world(par) if par>=0 else np.eye(4))@L
        return W[n]
    F=np.diag([1,-1,-1])   # OBJ space <-> game space
    tris=[]
    for n,v in nodes.items():
        if not v['draws']: continue
        part,piece=v['draws'].rsplit(' ',1)
        if part not in sel: continue
        piv=world(n); M=piv.copy()
        # joint matrix = pivot x T(-pivot position); OBJ vertices are joint space
        M=M@np.vstack([np.hstack([np.eye(3),-np.array(st[n][1] or (0,0,0)).reshape(3,1)]),[0,0,0,1]])
        path='%s/%s/%02d/piece%s.obj'%(models,part,sel[part],piece)
        if not os.path.exists(path): continue
        Vs,Ts,Fs,mtl=load_obj(path)
        Vg=[(M@np.append(F@np.array(p),1))[:3] for p in Vs]
        for mname,cs in Fs:
            m=mtl.get(mname,{}); col=(200,200,200)
            tex=m.get('tex')
            if tex is not None and cs[0][1]:
                u=sum(Ts[c[1]-1][0] for c in cs)/3; vv=sum(Ts[c[1]-1][1] for c in cs)/3
                col=tex.getpixel((min(tex.width-1,max(0,int(u*tex.width))),min(tex.height-1,max(0,int((1-vv)*tex.height)))))
            elif 'kd' in m: col=tuple(int(255*x) for x in m['kd'])
            tris.append(([Vg[c[0]-1] for c in cs],col))
    S=420; img=Image.new('RGB',(S*2,S),(30,34,40)); dr=ImageDraw.Draw(img)
    for col,yaw in enumerate((0.6,3.14159)):
        T=[]
        for vs,c in tris:
            q=[]
            for v in vs:
                x,y,z=v[0],-v[1],v[2]; x,z=x*math.cos(yaw)+z*math.sin(yaw),-x*math.sin(yaw)+z*math.cos(yaw); q.append((x,y,z))
            nrm=np.cross(np.subtract(q[1],q[0]),np.subtract(q[2],q[0])); sh=0.45+0.55*abs(nrm[2])/(np.linalg.norm(nrm)+1e-9)
            T.append((sum(x[2] for x in q),[(col*S+S/2+x[0]*0.3,S/2-(x[1]-430)*0.3) for x in q],tuple(int(k*sh) for k in c)))
        for _,pts,c in sorted(T,key=lambda t:-t[0]): dr.polygon(pts,fill=c)
    img.save(out)
