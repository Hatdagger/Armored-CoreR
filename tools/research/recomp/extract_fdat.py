#!/usr/bin/env python3
"""Copy the game's engine modules out of your own Master of Arena disc image.

Reads \\GG\\COM\\FDAT.T from the disc and writes these entries as separate files, so the RAM map
tool can scan the code the game loads at 0x80050DD8:

    206 menus   207 missions   208 link versus   209 split-screen VS   210 arena   211 replay

How to use (Windows): put this file next to your disc's .cue (or .bin / .iso) and double-click it,
or run   python extract_fdat.py "path\\to\\disc.cue"
The files land in a folder called fdat_out next to this script. Nothing on the disc is changed.
Only share the output privately for research; it is the game's own code.
"""
import glob, hashlib, os, struct, sys

WANT = {206: "menus", 207: "missions", 208: "link", 209: "vs", 210: "arena", 211: "replay"}
FDAT_PATH = ["GG", "COM", "FDAT.T"]


class Disc:
    def __init__(self, path):
        bin_path, sector, offset = path, 2352, 24
        if path.lower().endswith(".cue"):
            bin_path = None
            for line in open(path, encoding="latin-1"):
                s = line.strip()
                if s.upper().startswith("FILE") and bin_path is None:
                    name = s[s.index('"') + 1:s.rindex('"')] if '"' in s else s.split()[1]
                    bin_path = os.path.join(os.path.dirname(os.path.abspath(path)), name)
                if "MODE1/2352" in s.upper():
                    sector, offset = 2352, 16
                elif "MODE1/2048" in s.upper():
                    sector, offset = 2048, 0
            if not bin_path:
                raise SystemExit("The .cue names no data file.")
        elif path.lower().endswith(".iso"):
            sector, offset = 2048, 0
        self.f = open(bin_path, "rb")
        self.sector, self.offset = sector, offset
        # Some .bin dumps are plain 2048-byte images; check for the ISO marker.
        if self.read(16, 1)[1:6] != b"CD001":
            self.sector, self.offset = 2048, 0
            if self.read(16, 1)[1:6] != b"CD001":
                raise SystemExit("This does not look like a PlayStation disc image.")

    def read(self, lba, count):
        out = bytearray()
        for i in range(count):
            self.f.seek((lba + i) * self.sector + self.offset)
            out += self.f.read(2048)
        return bytes(out)


def find(disc, parts):
    pvd = disc.read(16, 1)
    root = pvd[156:156 + 34]
    lba, size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
    for i, want in enumerate(parts):
        data = disc.read(lba, (size + 2047) // 2048)
        p, hit = 0, None
        while p < len(data):
            n = data[p]
            if n == 0:
                p = (p // 2048 + 1) * 2048
                continue
            nl = data[p + 32]
            name = data[p + 33:p + 33 + nl].decode("latin-1").split(";")[0].upper()
            if name == want.upper():
                hit = (struct.unpack_from("<I", data, p + 2)[0], struct.unpack_from("<I", data, p + 10)[0])
                break
            p += n
        if not hit:
            raise SystemExit("Could not find \\" + "\\".join(parts[:i + 1]) + " on the disc. Is this Master of Arena disc 1?")
        lba, size = hit
    return lba, size


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    if len(sys.argv) > 1:
        path = sys.argv[1]
    else:
        found = sorted(glob.glob(os.path.join(here, "*.cue"))) or sorted(
            glob.glob(os.path.join(here, "*.bin")) + glob.glob(os.path.join(here, "*.iso")))
        if not found:
            raise SystemExit("Put this script next to your disc's .cue / .bin / .iso, or pass the path.")
        path = found[0]
    print("Disc:", path)
    disc = Disc(path)
    lba, size = find(disc, FDAT_PATH)
    head = disc.read(lba, 1)
    count = struct.unpack_from("<H", head, 0)[0]
    need = (2 + 2 * (count + 1) + 2047) // 2048
    head = disc.read(lba, need)
    starts = struct.unpack_from("<%dH" % (count + 1), head, 2)
    print("FDAT.T: %d entries, %d bytes" % (count, size))
    out_dir = os.path.join(here, "fdat_out")
    os.makedirs(out_dir, exist_ok=True)
    lines = []
    for k, what in WANT.items():
        if k + 1 > count:
            print("  entry %d missing" % k)
            continue
        a, b = starts[k], starts[k + 1]
        data = disc.read(lba + a, b - a)
        name = "fdat_%03d_%s.bin" % (k, what)
        open(os.path.join(out_dir, name), "wb").write(data)
        line = "%s  %7d bytes  sha1 %s" % (name, len(data), hashlib.sha1(data).hexdigest())
        print(" ", line)
        lines.append(line)
    open(os.path.join(out_dir, "README.txt"), "w").write(
        "Extracted from: %s\nFDAT.T at sector %d, %d entries\n\n%s\n" % (os.path.basename(path), lba, count, "\n".join(lines)))
    print("Done. Files are in", out_dir)


if __name__ == "__main__":
    try:
        main()
    except SystemExit as e:
        if e.code not in (None, 0):
            print(e.code)
    if os.name == "nt" and len(sys.argv) == 1:
        input("Press Enter to close.")
