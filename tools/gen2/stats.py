"""Dump the AC2 parts table from the F block to JSON.
Usage: AC2_F=<F folder> python3 stats.py SLUS_200.14 out.json
Record: name[18] type[18] u32 price, u8 maker, desc[73], then stats at +114
(u16 weight, u16 energy drain, ...). See docs/research/gen2-archive.md."""
import sys, os, struct, json, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from join import pkg, info
def rec(i):
    p = pkg(i)
    o = info(p) if p else None
    return p[o:o + 0x140] if o is not None else None
def elf_codes(path):
    b = open(path, 'rb').read()
    return [m.group().decode().split('/')[-1] for m in re.finditer(rb'/a2/ac2/model/ac/parts/[\x21-\x7e]+', b)]
if __name__ == '__main__':
    out = []
    for i in range(3400, 3802):
        r = rec(i)
        if not r: continue
        out.append(dict(f_id=i, name=r[:18].decode().strip(), type=r[18:36].decode().strip(),
            price=struct.unpack_from('<I', r, 36)[0], maker=r[40], desc=r[41:114].decode('latin1').strip(),
            weight=struct.unpack_from('<H', r, 114)[0], energy_drain=struct.unpack_from('<H', r, 116)[0],
            raw_stats=r[114:178].hex()))
    json.dump(dict(parts=out, elf_codes_in_order=elf_codes(sys.argv[1])), open(sys.argv[2], 'w'), indent=1)
    print(len(out), 'parts')
