# The Arena (roadmap step 7)

Main menu > **ARENA**: Master of Arena's ranked ladders, fought with the
garage's own AC, paying prize money and prize parts into the garage.

- Rules: `src/gen1/acr_gen1_arena.{h,c}` (no OS calls), tests in
  `tests/test_gen1_arena.c`.
- Screens: `src/platform/sdl3/arena.{h,c}`; standings saved to
  `arena.toml` next to the garage save (`ACR_ARENA_FILE` overrides).
- Opponents and prizes come from the import (`<import>/arena/`, the same
  files Rogue Arena reads; `prize_money` and `prize_part` are already in
  them). No re-import needed if Rogue Arena works.

## How it plays

```mermaid
flowchart TD
    M["Main menu > ARENA"] --> L["Arena list<br/>(main + 8 Ex Arenas, STAGE)"]
    L --> R["Ladder: RANK, PILOT, CHALLENGE / REFERENCE ONLY"]
    R -- "A on an opponent above" --> Q{"CHALLENGE ...?"}
    Q -- YES --> F["Fight: your garage AC vs their AC and pilot"]
    F -- win --> W["YOU WIN: up one rank,<br/>prize money, maybe a part"]
    F -- "loss / draw / give up" --> X["YOU LOSE: rank stays"]
    W --> R
    X --> R
```

## Rules

From the AC1Recomp mapping (`docs/mapping/arena-prizes.md`,
`menus-screens.md`, `moa_arena_run_boot.c`):

| Rule | |
|---|---|
| Main arena | 50 places: ranks 1-15 the **ranked** list, 16-50 the **sub arena** (L1 / R1 flips the pages) |
| Sub arena | each opponent is beaten once and can't be fought again ("BEATEN") |
| Ranked list | opens at the top of the sub arena (rank 16) |
| Winning | anyone above can be challenged; a win moves you **up one rank** |
| Prize money | the first win over an opponent only (user, 2026-11-24); credits cap at 99,999,999 |
| Prize parts | 23 parts on the disc; the part goes into the garage (+1 copy, +1 more for paired back weapons) |
| Champion / Guest | their part comes only once every opponent there is beaten |
| Ex Arenas 1-4 | need the matching legs (humanoid, four legs, reverse joint, caterpillar) |
| Blocked fights | the game's messages: "REFERENCE ONLY", "UNABLE TO CHALLENGE AT THIS TIME", "REQUIRES A SPECIFIC LEG TYPE", "YOUR EQUIPMENT IS INCOMPLETE" |
| No timer | a fight goes on until one side is down or out of the area, or you give up (Esc twice / Back + Start), which is a loss (user, 2026-11-25) |
| Out of area | inside the stage's whole-level box but outside both inner boxes loses, as in the game (Garden Plaza, Night City, Ocean Platform; Green Silo is closed in) |

## Our own choices (the mapping doesn't say) — confirmed by the user 2026-11-25

1. **Who drops when you win:** the beaten opponent drops to just below you;
   the ones in between move up one. This keeps the sub arena from ever
   leaving you with only beaten opponents above you.
2. **Parts only the first time** an opponent is beaten (money too: user's call).
3. **When the Ex Arenas open:** the four leg arenas at main-arena rank 15;
   Champion, Guest and FromSoft at main-arena rank 1; Master once Champion,
   Guest and FromSoft have each been topped. (`ACR_ARENA_UNLOCK=1` opens
   them all for testing.)
4. **Giving up counts as a loss.** With no timer a draw only happens when
   both sides go down at the same moment; that counts as a loss too.
5. **Prize 22** (Champion's) is a part outside the ten part tables; the
   result screen says the build doesn't have it yet.

## Stages

Pick with STAGE on the arena list (left / right). All four come from the
disc import; out of area (OoA) boxes are the stage's own, read from the
disc by acr-import (re-import to get them):

| Stage | Id | Out of area |
|---|---|---|
| Green Silo | 27 | none (closed dome) |
| Ocean Platform | 39 | yes |
| Garden Plaza | 25 | yes |
| Night City | 36 | yes |

Walking out at the edges is stopped by walls on all three; you leave the
area by flying over the edge. Leaving the inner area shows the warning,
leaving it completely loses the fight.

## Testing

- `ACR_ROGUE_AUTOWIN=1` now also wins Arena fights a moment after they
  start (headless screen tests).
- Tested here with a made-up import (no disc): list, ladder, challenge,
  fight with and without a clock, win, pay-out and save. Real opponents,
  their ACs on the ladder panel and the prize part names need the disc.

## Stage lights (2026-11-25, branch `stage-lights`)

Every Arena stage is lit the game's way: each area uses its own light set
from the disc (three directional lights, a colour matrix and a back
colour), and every face gets the colour the game's GTE gives it (details:
`docs/research/gen1-stages.md`, "Stage lights"). Green Silo's middle floor
and dome use the dim, green-tinted set 10, so it's darker than before;
Night City's buildings use set 1 (dimmer, cooler); Ocean Platform and
Garden Plaza use set 0 (floors a little brighter than the texture). Needs
a re-import (`acr-import`); older imports and custom maps keep the fixed
light. No fog: the game's stage draw doesn't use it (it only exists to hide
the PS1's short draw distance), so it stays off.

## Still to do (step 7)

- The VS screen before an Arena fight (Rogue Arena has one), opponent
  emblems on the ladder, the "Opponent unavailable, default win" rule
  (opponents killed in missions) once missions count.
