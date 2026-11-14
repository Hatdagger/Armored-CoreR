import sys,struct; sys.path.insert(0,'.')
from moa_model import parse
from PIL import Image,ImageDraw
def render(path,objs_sel,out,use_pivot=True,yaw=0.6):
    d=open(path,'rb').read(); m=parse(d)
    import math
    tris=[]
    for i in objs_sel:
        o=m['objs'][i]
        sc=struct.unpack_from('<7I',d,0x18+i*28)[6]
        px,py,pz,_=struct.unpack_from('<4h',d,0x18+sc) if use_pivot else (0,0,0,0)
        V=[(x+px,-(y+py),z+pz) for x,y,z in o['verts']]
        for p in o['prims']:
            vs=[V[k] for k in p[2]]
            if len(vs)==4: tris+= [(vs[0],vs[1],vs[2]),(vs[1],vs[3],vs[2])]
            else: tris.append(tuple(vs))
    W=H=512; img=Image.new('RGB',(W*2,H),(30,34,40)); dr=ImageDraw.Draw(img)
    allp=[v for t in tris for v in t]
    for view,(ya,ox) in enumerate(((yaw,0),(yaw+1.57,W))):
        c,s=math.cos(ya),math.sin(ya)
        def tr(v): return (v[0]*c+v[2]*s, v[1], -v[0]*s+v[2]*c)
        P=[tr(v) for v in allp]
        mn=[min(p[k] for p in P) for k in range(2)]; mx=[max(p[k] for p in P) for k in range(2)]
        sc=0.9*min(W/(mx[0]-mn[0]+1),H/(mx[1]-mn[1]+1)); cx=(mn[0]+mx[0])/2; cy=(mn[1]+mx[1])/2
        T=[]
        for t in tris:
            q=[tr(v) for v in t]
            a=[q[1][k]-q[0][k] for k in range(3)]; b=[q[2][k]-q[0][k] for k in range(3)]
            n=(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]); l=math.sqrt(sum(x*x for x in n))+1e-9
            sh=0.35+0.65*abs(n[2]/l)
            T.append((sum(x[2] for x in q)/3,[(ox+W/2+(x[0]-cx)*sc,H/2-(x[1]-cy)*sc) for x in q],sh))
        for z,pts,sh in sorted(T,key=lambda t:-t[0]):
            g=int(200*sh); dr.polygon(pts,fill=(g,g,int(g*1.05)),outline=(20,20,20))
    img.save(out)
if __name__=='__main__':
    render(sys.argv[1],[int(x) for x in sys.argv[2].split(',')],sys.argv[3],sys.argv[4]!='0' if len(sys.argv)>4 else True)
