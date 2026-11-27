# Feel pass: the blade and taking hits

Branch `feel/blade-hit` (2026-11-27). Goal: the blade and being hit behave
like Master of Arena. Fight rules are MoA's; content stays Phantasma's.
Done unattended (no play-testing), so every call made is written down here.
Guns, missiles (`feel/weapons`) and lock-on (`feel/lockon`) are untouched.

Prerequisites checked first: `feel/movement` (#34) and `feel/boost-energy`
(#35) are merged into main.

Sources, in the order they win:

1. **Disc**: MoA (USA) `GG/COM/FDAT.T` entry 209 (VS engine, loaded at
   `0x80050DD8`), disassembled for this pass; part values from `acr-import`.
2. **MoA recomp research** (`ma-work`, and what is already in
   `docs/research/`), including the harness measurements of the lunge.
3. The user's design rules (modern controls).

Units: speeds 1/64 world units per Gen 1 tick; the engine still runs 60
steps a second and fits the game's 22 ticks between them.

## What changed

| Area | Old | New | Source |
| --- | --- | --- | --- |
| **Air slash** | a swing started in the air ran the ground swing's rules (measured lunge, steering for 7 ticks, hits on lunge ticks 2-3, then hang) | its own handler: each tick it predicts the target's position (5 passes at the AC's air top speed), turns toward it at **4x** the top turn rate, and flies to a point short of it by both hit radii x 1.25: horizontal velocity = the gap, capped to +- the air top speed; vertical: climbs (by at most rise accel x 4 a tick more) or falls 256 a tick more (max 9,600). No target: velocity kept, boost held rises, otherwise falls. Blade line tested on frames **6 and 7** only, with the blade's **attack x 3** | VS jump table `0x80051370`: state 0x20 -> `0x80084ED8` (animation 0x38 = 56, handler `0x8007EB48`), state 0x21 -> `0x80084FA4` (animation 0x2F = 47, handler `0x8007F264`). Swing start sets 0x21 when AC +0x46 = 0xC0 (airborne). `0x8007F65C`: `attack + attack x 2` into the hit record before the sweep `0x8007F0C0` |
| Air slash length | animation 56 | animation **47** (fallback: the measured 1,450 ms) | `0x80084FA4` ends the state when animation 0x2F is done |
| **Stun restarts** | a new hit during a running stun did nothing | every stunning hit restarts the stun at its own level and adds its push: rapid fire can hold an AC (MoA stun-lock) | `0x8007BC1C` writes AC +0xDA = level on every call, no check |
| **Stun ends the victim's swing** | a swing kept going through a stun | a stunning hit ends the hit AC's own swing (no lock drop, no modern recovery) | hit callback `0x80082430` overwrites AC +0x45 with 0x80 / 0x81 and sets the busy bit, whatever state it was in |
| Crouched (cannon stance) hit | nothing at all | pushed, no stun | `0x800825E0`: state 0x22 -> `0x8007B454` with the push as velocity |
| **Swing sound** (#23, id 15) | on the press | on swing **frame 8** (ground) / **frame 5** (air), at volume 120 | `0x8007EC24` / `0x8007F5C8` |
| **Blade-hit flash** | none (only shots made hit effects) | the energy-hit flash (glow + flare) at the hit AC's middle | hit dispatcher `0x8008821C` -> spark `0x8008C164`; blades are energy hits (flags 0x20). Exact sprite not mapped: **stand-in look** |
| Replay / netplay rules | 5 | **6** | fights play out differently; old replays are refused. `air_swing` and `blade_hit_seq` are in the match hash; replay golden numbers updated |

Engine: `acr_gen1_battle.h` ("Air slash", hit stagger notes),
`air_slash_tick` and `blade_step` / `stagger_hit` in `acr_gen1_battle.c`,
`in.swing_air` in the sim, `sfx.c` and `fx.c`. Tests: air slash damage is a
multiple of the x3 hit; two ACs swinging face to face, the first cut stops
the other's swing.

## Checked, unchanged

| Value | Engine (default AC) | Source |
| --- | --- | --- |
| Own button | Circle (pad EAST, "BLADE"), not in the weapon cycle | `controls.c`; VS player block +0x29C |
| Swing start | new press; energy **above** the charge drain, which it takes; not drained, not mid-swing, not stunned | `0x80083224` |
| Target search | nearest enemy up to 3,000 ahead within +-45 degrees | `0x80078230` |
| Ground steering | toward the target at the top turn rate until the lunge (7 ticks) | `0x8007EB48` |
| Ground lunge | measured 125, 125, 125, 50, 44, 24, 23 a tick from 330 ms (516), stopped at body contact | recomp harness |
| Ground hit window | two crossings, reach 450 / 420: 2 hits from 650 and closer, 1 at 700, none at 800 | harness fit of frames 7-11, two line tests a frame, no "already hit" guard |
| Hit push on the blade victim | the hit push (impact x 3.2 a tick, max 98) plus 10 back | `0x80079ADC` |
| Stun length | knockback level 1-5 = ticks with no controls; impact 0 (energy guns) none | `0x80079AA4`, `0x8007FECC` |
| Energy per swing | the blade's charge drain (LS-2001: 2,050) | blade +0x2C |
| Locks | every lock dropped when the swing ends (MoA; Phantasma keeps them) | `0x8007EFDC` |
| Hit sound | #05 (id 6, the hit table's loud record) on each hit; #34 when destroyed | hit table `0x800930A0`; wreck `0x8008E8FC` |
| Recovery | the original pad has none beyond the swing's own animation (ends frame 12 at the earliest) | `0x8007EFB8` |

## Modern controls

Unchanged and still percentages of the base: `blade_en_pct` (125 % of the
charge drain), `steer_pct` (115 %: now also scales the air slash's 4x turn),
`recover_hit_ms` / `recover_miss_ms` (100 / 300 ms standing still after a
swing). A swing cut short by a stun gives no recovery.

## Calls made without the user (to revisit in play)

- **Air slash x3 is a big change.** It is plainly in the code, but nobody
  has felt it here yet. If a re-measurement in the recomp disagrees
  (air slash on the default AC should take 738 x 3 = 2,214 attack, about
  738 AP a hit), the recomp wins over this reading.
- The air slash's rising energy cost (`0x8007E698` / `0x8007E540`, also run
  by the ground swing on frames 3-11) is **not modelled**: unread routines.
- The air slash's sweep `0x8007F0C0` is assumed to test one line a frame
  (the ground sweep tests two). Not checked.
- Blade waves keep the ground swing's window in the air; the air handler
  calls other routines (`0x8007E5B4` / `0x8007E5D8`) around frame 11, unread.
- No vertical part of the hit push yet (the game adds half of it in the
  air), and the "upward push makes an air stun" rule (push y < -29) is not in.
- Heavy hits get no special sound: the disc has none (only the hit table and
  the wreck explosion), so #34 stays for destruction only.
- The blade-hit flash look is a stand-in until the flash table entry the
  blade uses is mapped.
