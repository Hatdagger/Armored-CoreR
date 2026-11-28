"""Read every F-block JOIN package from one stitched stream (works for AC2 and Another Age).

File IDs are not reliable for AA: the package for an ID can start 0, 32 KB or 64 KB into
the file (the archive table offsets drift), so instead the whole block is joined and
scanned for 'JOIN'. Each package says its section kinds (JOIN+6..: 0 model, 1 texture,
2 settings, 3 name/stats record) and its index within its category (JOIN+0x10).

Usage: python3 catalogue.py F_FOLDER OUT.json
"""
import sys, os, glob, struct, json

def stream(folder):
    parts = []; ids = []; off = 0
    for p in sorted(glob.glob(folder + '/*.bin')):
        b = open(p, 'rb').read(); ids.append((int(os.path.basename(p)[:5]), off)); parts.append(b); off += len(b)
    return b''.join(parts), ids

def packages(folder):
    b, ids = stream(folder); out = []; i = b.find(b'JOIN')
    while i >= 0:
        sizes = struct.unpack_from('<8I', b, i + 0x14); offs = struct.unpack_from('<8I', b, i + 0x34)
        kinds = list(b[i + 6:i + 0x10])
        n = sum(1 for o in offs if o != 0xffffffff)
        ok = 0 < n <= 8 and all(offs[k] != 0xffffffff and 0 < sizes[k] < 0x80000 for k in range(n))
        if ok:
            rec = None
            for k in range(n):
                if kinds[k] == 3:
                    o = i + offs[k]; r = b[o:o + sizes[k]]
                    if len(r) >= 118 and all(32 <= c < 127 for c in r[:36]):
                        rec = r
            file_id = max(x for x in ids if x[1] <= i)[0]
            e = dict(at=i, file=file_id, group_count=b[i + 4], index=b[i + 0x10], kinds=kinds[:n],
                     sizes=list(sizes[:n]), offsets=list(offs[:n]))
            if rec:
                e.update(name=rec[:18].decode().strip(), type=rec[18:36].decode().strip(),
                         price=struct.unpack_from('<I', rec, 36)[0], maker=rec[40],
                         desc=rec[41:114].decode('latin1').strip(),
                         stats=list(struct.unpack_from('<%dH' % ((len(rec) - 114) // 2), rec, 114)))
            out.append(e)
            i = b.find(b'JOIN', i + 0x60)
        else:
            i = b.find(b'JOIN', i + 4)
    return out

if __name__ == '__main__':
    p = packages(sys.argv[1])
    json.dump(p, open(sys.argv[2], 'w'), indent=1)
    print(len(p), 'packages,', sum(1 for x in p if 'name' in x), 'named')
