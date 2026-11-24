"""Mission units' motion sets (FDAT 5s+4 chunk 12): per object handler a
36-byte header and a motion set in the AC format (acr_motion_moa.c): the
root's displacement since the animation began, in 1/4 world units.
Usage: moa_unit_motion.py <import>/unpacked <map> [anim,anim,...]
(docs/research/gen1-stages.md, "Mission script"). Reads your own import."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import moa_stage  # noqa: E402
import moa_units  # noqa: E402


def motion_sets(root, stage):
    """[(header 36 bytes, motion set bytes)], index = handler - 1."""
    c = moa_stage.chunks(moa_units.stage_file(root, stage))[12]
    n = struct.unpack_from('<H', c, 0)[0]
    o, out = 4, []
    for _ in range(n):
        size = struct.unpack_from('<I', c, o)[0]
        e = c[o + 4:o + 4 + size]
        o += 4 + size
        out.append((e[:36], e[36:]))
    return out


def anims(ms):
    """{anim: dict(len, rot, keys [(frame, x, y, z)])}."""
    if len(ms) < 12:
        return {}
    out = {}
    for a in range(struct.unpack_from('<I', ms, 0)[0]):
        p = 12 + struct.unpack_from('<H', ms, 12 + 2 * a)[0]
        _, _, length, kc = struct.unpack_from('<4H', ms, p)
        keys = [struct.unpack_from('<H3h', ms, p + 8 + 8 * i) for i in range(kc & 0x7FFF)]
        out[a] = dict(len=length, rot=bool(kc & 0x8000), keys=keys)
    return out


def main():
    root, stage = sys.argv[1], int(sys.argv[2])
    want = [int(x) for x in sys.argv[3].split(',')] if len(sys.argv) > 3 else None
    for h, (_, ms) in enumerate(motion_sets(root, stage)):
        a = anims(ms)
        print('handler %d: %d animations' % (h + 1, len(a)))
        for k in sorted(a):
            if want is not None and k not in want:
                continue
            x = a[k]
            if not x['keys'] or x['keys'] == [(0, 0, 0, 0)]:
                continue
            last = x['keys'][-1]
            print('  anim %2d: %3d frames, ends at x %d y %d z %d (%d keys)'
                  % (k, x['len'], last[1], last[2], last[3], len(x['keys'])))


if __name__ == '__main__':
    main()
