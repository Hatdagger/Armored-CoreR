# Research tools from the recomp repos

Small scripts that read files from **your own** Master of Arena disc. None contain game data. Run with `python3 -I`.

| Script | What it does |
|---|---|
| `extract_fdat.py` | Copies the engine modules (FDAT 206-211) out of your disc image |
| `engine_xlate.py` | Finds where an address in one fight engine lives in another |
| `weapon_map.py`, `weapon_map_tables.py` | Builds the weapon sheet behind `docs/research/recomp/mapping/weapon-map.md` |
| `moa_fx_atlas.py` | Draws the fight effect sprites as one picture (needs Pillow) |
| `moa_parts_dump.py` | Lists the part tables |
| `disc_entry.py`, `mips_dis.py` | Find a file on the disc; read PS1 machine code as text |
