# Feel pass: weapons

Branch `feel/weapons` (2026-11-27). Goal: weapons behave like Master of
Arena. Fight rules are MoA's; content stays Phantasma's. Done unattended,
so every call made is written down here. Lock-on / FCS (chat `feel/lockon`)
and the blade (chat `feel/blade-hit`) were not touched.

Sources, in the order they win:

1. **Disc**: MoA (USA) `GG/COM/FDAT.T` entry 209 (VS engine, loaded at
   `0x80050DD8`), read with a MIPS disassembler; the 72 animation sets
   (`GG/P0-P3/PA00-71.T` entry 1).
2. **MoA research** (`ma-work`; the AC1Recomp mapping in
   `docs/research/recomp/mapping/`, mostly `stat-block.md` and
   `weapon-map.md`).
3. The user's own rules (ammo, modern controls).

Units: "tick" = one Gen 1 game step (22 a second). The engine still runs 60
steps a second and keeps weapon counters in ticks; nothing here changes that.
Speeds are world units a tick.

## What changed

| Value | Old | New | Source | Notes |
| --- | --- | --- | --- | --- |
| Post-shot hold (firing animation) | 7 + 2 x detail +4 (stand-in) | **7 + detail +4** ticks | `0x800822A8` sets AC +0x6D = the hold byte and +0x7C = 0; the layer-2 player `0x80054DE4` counts +0x7C up to (set table at set +8)[0] + that byte, then +0x6D = 255. The base is **7 in all 72 animation sets** (read from the disc) | WG-RF35 / WG-PB26 (0) stay 7 (the measured 13-tick rifle cadence still holds), cannons WC-GN230 / WC-IR24 (1) 8 instead of 9, WX-ED2 / WX-C/4 (2) 9 instead of 11. `demo_data.c`. |
| Weapon switch delay | at once (blocked during a missile volley) | the pick waits until the **refire wait is 0** | `0x80082F68`: a press advances player block +0x11, skipping slots with no ammo, plays sound 45, drops the locks and clears the volley (+0x47 = 0); `0x80083044`: only with AC +0x48 = 0 does +0x11 become the weapon (+0x10) | So a switch waits out the reload, and the post-shot hold too (the wait doesn't count during it). A second press counts on from the pending pick. A press during a volley now cuts the volley short, as in the game, instead of being ignored. Leaving the blade (not in the game's cycle) stays immediate. Fields `switch_pending`, `switch_to`. |
| Switching while kneeling | the stance just dropped | **stands up** (state 0x23, below) | `0x800830A8` | |
| Cannon stance exit | at once, on any move without fire | **stands up over the kneel's own length**, controls held, and only once the refire wait is 0 | `0x800822C0`: with +0x48 = 0, legs class 0 and a move button (forward, back, both strafes, boost: masks +0x298 / 28E / 290 / 29E / 2A2), state 0x23; its handler `0x80085050` replays the kneel animation backwards (+0x78 = -1) and lets go of the controls (busy bit 0x10) when it ends | Moving while the cannon reloads keeps you down. Fire held keeps firing in the stance. Field `stand_ms`; the pose stays crouched meanwhile. |
| Cannon stance entry | fire held on the ground | the same, **not while boosting**; the kneel can't be cut short | `0x80082340`: not in class 0xC0 (air) or 0xB0 (ground boost); the stand-up test only runs once animation 45 has finished | Entry is one tick after the press in the game (it wants fire held and not freshly pressed); not modelled. |
| Cannon kick on the firing AC | none (stand-in) | **kick world units a tick, opposite the shot** | `0x80081E80`: kick = player block +0x214 x cannon table `0x80091310` +2 >> 8, taken off the AC's motion along the normalised aim. +0x214 = slot +9 x 75 / 32 + 1 (`0x80082894`); slot +9 for cannons = detail +2 x (4,169,728 / S) x (32,768,000 / W) >> 24, 1-64 (S legs stability, +800 SP-SAP; W carried weight; stat-block.md) | `acr_gen1_cannon_kick`, applied once a trigger for stance (kind 2) cannons. Crouched, the sim's stance slide then stops it (1/8 a tick, 0 below 512). Human PLUS two-legged (body frame 3) on the ground: none (`0x80081E60`). The pad rumble that goes with it is not modelled. Guns and missiles have no kick: +0x214 is read nowhere else. |
| Cannon table kick value | not imported | `acr-import` writes **`cannon_kick`** (table +2: 32 for most models, 256 for some) | `0x80091310` + 6 x model + 2 | **Re-import the disc** to get it; older imports use 32. Base recoil is the existing `fire_pattern` (detail +2). |
| Shot sounds | by type name (machine guns on #14, every cannon / rocket / bazooka on #12) | the game's per-weapon fire sound ids through `sound-labels.md`: id 4 -> **#21**, 48 -> **#15**, 50 -> **#20**, 51 -> **#07**, 53 -> **#22**, 0 -> **#12**, 2 -> **#18** | weapon-map.md "Fire snd" column | Rifles, hand guns, chain / gatling / slug / linear guns, bomb dispensers: #21. Machine guns #15, but WG-AR1000 and WA-Finger #21. Pulse #20. Laser / plasma rifles, WG-RF/E, AW-XC65, WC-XC8000: #07. Rockets, bazookas, flame, mines, arm cannons, WG-PB26, WC-01QL, WC-IR24, AW-XC5500: #22. WC-GN230, AW-DC/2, WX-ED2, WX-C/4: #12. Missiles #18. By name where the type doesn't settle it, else by type (so Phantasma's own weapons get the majority sound of their type). `sfx.c`. |
| AI weapon switch | pressed again while the pick differed from the current weapon | looks at the pending pick | | Otherwise it cycled past its choice while the switch waited. |
| Replay / netplay rules | 6 (after the blade pass) | **7** | | Weapons play out differently; the replay test's golden numbers are updated, the new fields are in the match hash. |

## Checked, unchanged

| Value | Engine (already the game's) | Source | Notes |
| --- | --- | --- | --- |
| Refire wait | detail +0x11 ticks; a wait of n fires every **n + 1** ticks; doesn't count during the post-shot hold | `0x80081020`-`0x80081040`, cannon `0x800823EC` | WG-MGA1 11 a second, WG-RF35 every 13 ticks (hold 7 + wait 5 + 1). |
| Shots per trigger | detail +2 bits 0-1 (1, 2, 4) | gun fire `0x80080DF4` | |
| Shot speed | per model from the five shot tables; cannons table `0x80091310` +0 | `0x80080E38` | Already imported (`shot_speed`). |
| Range | weapon range capped by the FCS class range; the shot is removed past it | | |
| Drop | **none for gun and cannon shots**: a shot leaves along the aim and flies straight (`0x80081150`). Missiles coast under gravity before ignition and the nose drops after the fuel. | | Bazookas and grenade launchers included: no arc in the game. |
| Missile flight | flight table `0x80093114` (fuel, life, thrust, top speed, turn a tick, give-up cone), kind 3 split, kind 4 vertical | `0x8008907C` | gen1-weapons.md. |
| Delay between volley missiles | a volley every **4 ticks** (locks x 4, fires on (+0x47 & 3) = 0), both sides at once | `0x800818F8`-`0x80081A34` | Re-read this pass. After the last, the fire interval. |
| Cannon stance by legs | player block +0x278: 0 two legs kneel; 2 can't fire in the air; 1 and 3 fire freely | `0x80081B74`-`0x80081C28` | Engine: four legs ground only, caterpillars free, Human PLUS (3) free. That 2 is four legs and 1 caterpillars is the most likely reading (both get the second movement handler), not proven. The four-leg rule is now from code, not only from a player's description. |
| Human PLUS no-kneel | kept | `0x800836D8`: two legs with Human PLUS level 4+ become class 3 | |
| Hit push on the target | unchanged (that is the hit-stagger work) | | |
| Ammo | no automatic swap; an empty weapon stays selected until switched away from, and can't be picked again | user, 2026-11-19; the game's pick skips empty slots too (`0x80083010`) | |

## Modern controls

The rocket and spread rules still sit on top of the base values: the
modern rocket refire wait is `ROCKET_ROF_PCT` of the (unchanged) fire
interval and the spread / bloom is added per shot as before. One knock-on:
since a switch now waits for the refire wait, a modern pilot's slower
rockets also take longer to switch away from. Left as is (it follows from
the base rule); worth a look in play.

## Open

- The missile flight loop (#19, sound id 12, plays with the launch) isn't
  hooked yet: it needs a held voice per missile in flight.
- The kneel's entry tick (one tick after the press) and the rumble are not modelled.
- `cannon_kick` needs a re-import; until then every cannon uses 32.
- Not play-tested (asked so). Worth a look: how far a heavy cannon pushes a
  light biped back in the stance.
