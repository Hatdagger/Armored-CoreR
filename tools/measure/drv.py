#!/usr/bin/env python3
"""drv.py <ops...> : drive the headless game on port 4470.
ops: p:<btn>[:frames]  w:<frames>  s:<name>  r:<addr>:<len>  wr:<addr>:<hex>  raw:<json>
btn names: start select up down left right tri cir x sq l1 r1 l2 r2"""
import json,socket,sys,time
B=dict(select=0x1,start=0x8,up=0x10,right=0x20,down=0x40,left=0x80,l2=0x100,r2=0x200,l1=0x400,r1=0x800,tri=0x1000,cir=0x2000,x=0x4000,sq=0x8000)
L='./'
def call(d):
    s=socket.create_connection(('127.0.0.1',4470),timeout=30); d['id']=1; s.sendall((json.dumps(d)+'\n').encode())
    buf=b''
    while not buf.endswith(b'\n'):
        c=s.recv(65536)
        if not c: break
        buf+=c
    s.close(); return json.loads(buf)
def frame(): return call({'cmd':'frame'})['frame']
def wait(n):
    t=frame()+n
    while frame()<t: time.sleep(0.02)
def read(a,n): return bytes.fromhex(call({'cmd':'read_ram','addr':'0x%08x'%a,'len':n}).get('hex',''))
if __name__=='__main__':
    for op in sys.argv[1:]:
        k,_,rest=op.partition(':'); a=rest.split(':')
        if k=='p':
            b=0
            for nm in a[0].split('+'): b|=B[nm]
            n=int(a[1]) if len(a)>1 else 3
            call({'cmd':'press','buttons':(~b)&0xffff,'frames':n}); wait(n+6)
        elif k=='w': wait(int(a[0]))
        elif k=='s': print(call({'cmd':'screenshot_file','path':L+a[0]+'.png'}).get('path'))
        elif k=='r': print(json.dumps(call({'cmd':'read_ram','addr':a[0],'len':int(a[1])})))
        elif k=='wr': print(json.dumps(call({'cmd':'write_ram','addr':a[0],'val':a[1]})))
        elif k=='raw': print(json.dumps(call(json.loads(rest))))
