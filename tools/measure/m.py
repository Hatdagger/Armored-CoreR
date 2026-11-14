import sys,struct,json,time; import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); import drv
def u32(a): return struct.unpack('<I',drv.read(a,4))[0]
def obj(i): return u32(0x80045198+928*i+920)
def state(p):
    b=drv.read(p,376)
    x,y,z=struct.unpack_from('<hhh',b,8); yaw=struct.unpack_from('<h',b,18)[0]
    ap=struct.unpack_from('<H',b,360)[0]>>2
    return dict(x=x,y=y,z=z,yaw=yaw,ap=ap,st=b[73])
