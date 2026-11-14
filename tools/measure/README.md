# Measuring Gen 1 feel in the recomp

Scripts that drive the [AC1Recomp](https://github.com/Hatdagger/AC1Recomp) build of
Master of Arena through its debug port, play scripted inputs, and record the
AC's state every logic step. Results go in `docs/research/`.

## Run the game

Build with `-DPSX_DEBUG_TOOLS=ON`, then:

```
ArmoredCoreMasterOfArena --headless --no-launcher --disc "<your disc 1>.cue" --debug-port 4470
```

Get into a fight (for example VS Mode > VS START; player 2 stands still when
nobody holds controller 2). VS Mode has a 2-minute time limit.

## Scripts

| Script | What it does |
| --- | --- |
| `probe.py <buttons> <frames> <name>` | holds buttons (`x+up`, `left`, `none`...) and saves the AC object and player block each step to `<name>.json` |
| `walk.py <buttons> <frames> <name>` | same, but records every write to the AC's position and facing, with the code address that wrote it |
| `an.py` | helpers to load a run and list the fields that changed |
| `m.py`, `drv.py` | debug-port helpers |

Button names: `select start up down left right tri cir x sq l1 r1 l2 r2`.
Frame counts are screen refreshes (about 2.8 per logic step).
