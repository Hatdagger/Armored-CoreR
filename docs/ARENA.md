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
    R -- "A on an opponent above,<br/>or a DEFEATED one below" --> Q{"CHALLENGE / REMATCH ...?"}
    Q -- YES --> V["VS screen: both ACs, emblem,<br/>count 3..1, shutters"]
    Q -- "YES, opponent absent" --> D["Opponent unavailable, default win:<br/>up one rank, no pay"]
    D --> R
    V -- "count out / A" --> F["Fight: your garage AC vs their AC and pilot"]
    V -- B --> R
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
| Sub arena | in the game each opponent is beaten once and can't be fought again; here they can, as a rematch (below) |
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
6. **Rematches** (user, 2026-11-25): whoever you beat drops to just
   below you and is marked **DEFEATED** on the ladder. You can fight them
   again in any arena, for fun: no rank change, no money, no part (it still
   counts in the ARENA REPORT, and Ex Arenas still need their legs). Beating
   the pilot just above you means taking their place; beating someone
   further up still moves you up one rank (the game's rule).

## The VS screen

Every Arena fight opens with a VS screen (the user's simpler pick,
2026-11-25; the code is shared with Rogue Arena, `src/platform/sdl3/vs_screen.{h,c}`):
your AC (blue, left), the opponent's (red, right) with their emblem from
your disc behind it (`<import>/arena/<AA-name>/<RR>-emblem.tim`), RANK
badges, the pilot names, VS (REMATCH under it for a rematch), "UNITS
DEPLOY IN 3..1" and the stage; then the hangar shutters close and the
fight starts. A skips the count, B goes back to the ladder. No strip in the
bottom band (Rogue Arena keeps its strip of small boxes and its 5 count).

Your own emblem: none yet. The garage has no EDIT EMBLEM and the save keeps
no emblem, so the left box shows your AC only.

## Emblems

Each opponent's emblem shows small on its ladder row and large behind its
AC on the ladder panel and the VS screen.

## Absent opponents: "Opponent unavailable, default win."

From the menu module's challenge code (FDAT 206, `0x800BF43C`; strings at
`0x80053FE0` / `0x80054004`). After YES, an opponent who can't fight and
hasn't been handled yet gives **"Opponent unavailable, default win."**: it
is marked handled and you move up one rank with no fight. Challenging a
handled one again (it sits below you, like a DEFEATED one) gives
**"Opponent is unable to compete."**; no rematch.

| | |
|---|---|
| Who is absent | Armored-CoreR: opponents killed in missions (missions hook in later through `acr_gen1_arena_set_absent`) |
| Pay | none: no prize money, no part (the game pays after a fight; this path has none) |
| Ladder | UNAVAILABLE on the row until then; afterwards it drops below you as after a win, counts as beaten and reads UNABLE |
| Save | `absent = [...]` per arena in `arena.toml` (by disc rank: 1 due, 2 given); older saves read as all present |

What makes an opponent absent in the game itself (the check just before,
`0x8007E164`) isn't mapped yet.

Test switch until missions count: `ACR_ARENA_ABSENT="0:49,0:48"` marks
(arena : disc rank) absent. Disc rank is the rank on a new game.

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
- Checked with the real disc (2026-11-25, Master of Arena USA): ladder
  names (Nine-Ball / Hustler One at rank 1 ... Angst / No Problem at 49),
  their ACs and emblems on the ladder panel, VS screen, a real win over Top
  Cap paying 10,000 C and HD-H10, a rematch, the Humanoid Ex Arena. All 23
  prize ids resolve to the part names in AC1Recomp's `arena-prizes.md`
  (prize 22 has no part table, as before).

## Still to do (step 7)

- Stage lights and backgrounds: PRs #19 and #21.
- Missions calling `acr_gen1_arena_set_absent` when an arena pilot dies.
- The player's emblem on the VS screen once EDIT EMBLEM exists.
