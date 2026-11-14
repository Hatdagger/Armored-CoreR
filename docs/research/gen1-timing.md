# Gen 1 timing: how fast Master of Arena's game logic runs

**Result:** one logic step per frame, at most one frame per 715 NTSC scanlines:
45.449 ms, or **22.0 steps a second**. A frame that takes longer runs late, and
the game slows down; it never catches up. Engine constant: `src/core/acr_gen1.h`.

Source: Armored Core: Master of Arena (USA, SLUS-01030), boot program
`SLUS_010.30` and the battle engines in `GG/COM/FDAT.T`.

## Where the battle engines come from

The boot program loads code modules from archive `COM\FDAT.T` (archive 2) into
RAM at `0x80050DD8` (the address stored at `0x800112F0`), through the
archive-entry loader at `0x80016A58`:

| FDAT entry | Size | What |
| --- | --- | --- |
| 206 | 604 KB | menus (entry point `0x800A69C8`) |
| 207-211 | 268-296 KB each | battle engines (arena, versus and mission variants) |

## The frame end

Every battle engine (entries 207-211) ends a frame the same way, for example
entry 207 at `0x8005D29C`:

1. `DrawSync(0)`: wait for the GPU to finish the frame.
2. Loop: `elapsed = GetRCnt(RCntCNT1) - frame_start` (boot program `0x80016724`,
   16-bit wraparound) until `elapsed >= 0x2CB` (715).
3. Swap draw and display buffers, start the next ordering table.
4. `frame_start = GetRCnt(RCntCNT1)`.

The same `slti 0x2CB` follows the call in entries 207 (`0x8005D2CC`), 208
(`0x8005CED4`), 209 (`0x8005C080`), 210 (`0x8005E2D0`) and 211 (`0x8005C624`).
There is no `VSync` wait in the battle loop; the only `VSync(2)` calls are in
small pause-style loops.

The frame start time is read only by this wait, so no game logic scales with
the real frame time: it runs one fixed step per frame.

## Root counter 1 counts scanlines

At boot (`0x80016C70`) the game calls `SetRCnt(RCntCNT1, 0xFFFF,
RCntMdNOINTR)`. The libetc `SetRCnt` (`0x8002B564`) turns that into counter
mode `0x148` for counter 1, whose bit 8 selects the horizontal-blank clock.

NTSC line rate: 53,693,175 Hz GPU clock / 3413 clocks per line = 15,732 lines
per second. 715 lines = 45.449 ms = 22.003 frames per second.

## Still to measure

715 lines is the fastest the game runs. Heavy scenes take longer per frame on
real hardware, so the speed players were used to may sit below 22 steps a
second. The recomp reproduces the original's timing, so its frame times in
typical fights (arena, versus, many effects on screen) will show how often
the game ran below the cap. That decides whether Gen 1 in this engine runs at
the cap (smoothest, as designed) or at a typical rate.
