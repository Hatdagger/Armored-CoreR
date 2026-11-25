# Making maps in Tinkercad

Build the map in Tinkercad, export it, drag the zip onto
`play-tinkercad-map.bat` and the game opens on it in AC TEST. Shapes and
colours carry over. Every shape you can see is solid: ACs stand on it, run
into it, and shots hit it.

## Quick start

1. In Tinkercad, make a new design. Build a floor, walls round it, and any
   cover you want (see **Size** below).
2. Add the **start markers**: a small box where each AC drops in, sitting on
   the floor.
   - **Green** = the players' side (team 1).
   - **Red** = the enemies' side (team 2).

   Tinkercad's own green and red work, as does any clear green or red;
   orange, yellow, pink and the like stay scenery. One or two markers a side.
   Two are teammates. The game puts teammates 10 mm (500 units) either side of
   their side's middle, side by side, so set a pair 20 mm apart. In a 1v1, one
   AC starts at each side's middle. Each side faces the other.

   The markers aren't part of the map. If a side has no marker, it starts on
   the map's north-south middle line, near the south edge (players) or the
   north edge (enemies). Magenta `FF00FF` and cyan `00FFFF`, the first
   colours, still work as players and enemies.
3. **Export → .OBJ** (the *Download for 3D Printing* panel). You get a zip.
4. Drag the zip onto **`play-tinkercad-map.bat`**. The map becomes stage 50
   and the game opens on it. Drag a new export on whenever you change the map.

Setting up once: copy `tools/maps/play-tinkercad-map.bat` and
`tools/maps/tinkercad_stage.py` next to `armored-corer.exe`, and install
Python 3 from python.org (tick *Add python.exe to PATH*). Nothing else is
needed.

## Big maps: tiles

Tinkercad's workplane is small, so a big map can be built in pieces that
join up like Lego: one Tinkercad design per tile, all on a grid.

- **Each tile is 200 × 200 mm** (the default workplane). Give every tile a
  full 200 × 200 floor plate, and keep everything inside it: the tile's
  middle is worked out from what's in it, so a short floor shifts it.
- **Name each export after its grid square** before dragging it on:
  letters go west to east, numbers south to north. `A1` is the south-west
  corner, `B1` is east of it, `A2` north of it.
- **Drag all the zips onto the .bat at once.** They're joined into one map.
  Where tiles meet, keep the floors at the same height and leave out the
  walls between them.
- **Reuse a tile** by putting several squares in its name: `C1 C2 C3 corridor.zip`
  fills all three.
- **Turn a tile** with `rot90`, `rot180` or `rot270` in its name (clockwise,
  seen from above): `D4 rot90 corner.zip`.
- Put the markers in tiles that are used only once.
- Tinkercad's top view: north is the back of the workplane.

A different tile size: `--tile 100` (mm) on the command line below.

## Size

**1 mm in Tinkercad = 50 game units.** Some measures to build by:

| Thing | Game units | Tinkercad |
|---|---|---|
| An AC's body (what hits walls) | 200 wide, 212 tall | 4 mm wide, about 4 mm tall |
| Highest step an AC walks up | 120 | 2.4 mm |
| Mission map tile | 2,500 | 50 mm |
| Green Silo, wall to wall | about 12,700 | about 254 mm |
| Default Tinkercad workplane | 10,000 | 200 mm |

For an arena the size of Green Silo, make the workplane bigger: **Settings →
Edit Grid**, 300 × 300 mm. A different scale: `--scale` (see below).

Anything taller than 2.4 mm stops a walking AC; anything lower it walks over.
For ramps, use the Tinkercad *Wedge*. ACs climb any slope gentler than 45
degrees.

## Things to know

- **Up is Tinkercad's up**, and the map isn't mirrored: seen from above in
  the game, it looks the same as in Tinkercad's top view.
- **Build walls or a roof.** An AC that walks off the floor's edge falls. Make
  the walls tall, or put a roof on (an open-top arena lets boosting ACs fly
  out).
- **Holes:** a Tinkercad hole only cuts once it's *grouped* with the shape it
  cuts. An ungrouped hole is exported as a solid shape.
- **Shapes can overlap or sink into each other.** Hidden faces inside a shape
  do no harm.
- **Colours:** up to 15. Extra, rarely used colours are merged into the
  nearest one. Tinkercad has no textures, so maps are plain colours. The
  disc's stages are lit with the game's own stage lights (after a
  re-import); custom maps have no light sets, so they keep one fixed light
  from above.
- **Round shapes** (cylinders, spheres) are fine. Raise their *Sides* setting
  for smoother walls. Very high side counts make big files.
- **Walls and floors** on these maps follow the engine's stand-in rules (the
  ones the disc's stages used before their own collision was read). They're
  close to the game's, but not exact.

## Without the .bat (any OS)

```
python tools/maps/tinkercad_stage.py my-map.zip <import folder> --id 50 --name "My Map"
python tools/maps/tinkercad_stage.py A1.zip B1.zip "A2 rot90.zip" <import folder> --id 51
ACR_STAGE=50 ./armored-corer
```

- `--id`: stage number. 0-40 are the disc's stages (a new disc import
  overwrites them), so use 50 and up. One map per number.
- `--name`: the name shown in the game.
- `--scale`: game units per mm (default 50).
- `--up z|y`: the file's up axis. The default works it out by itself. This
  only matters for an OBJ from another program (SketchUp, Blockbench and
  Blender also export OBJ; use the same colours for the starts).

The script writes `stages/<id>/` in the import folder: `stage.toml` (name,
starts), `visual.obj` / `visual.mtl` (what's drawn) and `collision.obj`
(what's solid), in the same layout acr-import writes for the disc's stages
(`docs/research/gen1-stages.md`, "Import"). It warns when a start has no floor
under it.

`tools/maps/make_sample.py` writes a small sample map the way Tinkercad
exports one (floor, walls, four pillars, a ledge, both markers), and with
`--tiles <folder>` three joining tiles. The `custom_stage` test converts both
and loads them with the engine's parsers (`tests/test_custom_stage.c`).

## Not yet

- Picking custom maps from a menu (for now it's `ACR_STAGE`, or the .bat).
  F7 in AC TEST still cycles through the disc's stages.
- Mission drops, enemy placement, out-of-area boxes, see-through or no-collision
  shapes, a sky.
