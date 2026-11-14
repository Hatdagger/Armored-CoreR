import json,sys,struct
def load(t): return [(f,bytes.fromhex(o),bytes.fromhex(s)) for f,o,s in json.load(open(f'./{t}.json'))]
def series(S,off,fmt='<h',src=1): return [struct.unpack_from(fmt,x[src],off)[0] for x in S]
def changing(S,src=1,step=2,fmt='<h'):
    n=len(S[0][src]); res={}
    for off in range(0,n-1,step):
        v=series(S,off,fmt,src)
        if len(set(v))>1: res[off]=v
    return res
