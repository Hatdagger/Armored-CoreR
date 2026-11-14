import struct
def tims(d):
    p=0; out=[]
    while p+8<=len(d):
        mag,fl=struct.unpack_from('<II',d,p)
        if mag!=0x10: break
        q=p+8; clut=None
        if fl&8:
            bl,cx,cy,cw,ch=struct.unpack_from('<IHHHH',d,q); clut=(cx,cy,cw,ch,q+12); q+=bl
        bl,ix,iy,iw,ih=struct.unpack_from('<IHHHH',d,q); img=(ix,iy,iw,ih,q+12); q+=bl
        out.append(dict(off=p,bpp=[4,8,16,24][fl&3],clut=clut,img=img)); p=q
    return out,p
if __name__=='__main__':
    import sys
    d=open(sys.argv[1],'rb').read(); t,end=tims(d)
    for x in t: print(x['bpp'],'clut',x['clut'][:4] if x['clut'] else None,'img',x['img'][:4])
    print('end',hex(end),len(d))
