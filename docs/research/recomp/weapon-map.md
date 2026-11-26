# Weapon map: shots, looks, sounds, impact and stun

Every weapon in Master of Arena, from its part record to what it fires, how the shot looks and sounds, what a
hit does. Static read of the USA disc 1 (SLUS-01030), 2026-11-20: menu module FDAT 206 (part tables) and the
**VS engine FDAT 209** (other fight engines carry the same tables). ✔ = run-tested; `?` = a guess.
Helper scripts made in chat (`weapon_map.py` → `weapons.csv`, `atlas.py`) are not in the repo yet.

Units: a **step** = one game tick, 22 a second. Speeds in world units a step. Angles 4096 = 360°.

## 1. The path of a shot

1. **Weapon detail record** (24 bytes, menu module; `mapping/parts-tables.md`): kind, model ID, attack, impact,
   flags, ammo, fire interval… copied into a weapon slot (player block +0x2D8 + 62 × slot; `mapping/stat-block.md`).
2. **Fire routine by kind**: gun `0x80080DF4`, missile `0x80081888`, cannon `0x80081B18`.
3. Guns: the model ID picks a family (spawner + table). Cannons: model ID 0–7 → jump table `0x800511CC`.
   Missiles: launcher table `0x800912B0`.
4. The spawner fills a **pool record** (256 × 228 bytes at `0x801D6F6C`): speed, life, the 6-byte hit record
   (flags, attack, impact), draw kind (+0x10), look fields, two packed effect words (§3), and plays the fire sound.
5. The update routine moves it and hit-tests. On a hit: debris word → chips (`0x8008C164`); hit dispatcher
   `0x8008821C` → damage (`0x800795DC`) → on-hit callback `0x80082430` (knockback, stun, rumble); flash word →
   impact flash, light, hit sound (`0x8008B60C`).

## 2. Shot families (guns, by model ID)

| Model ID | Family | Spawner / update | Table (record size) | Drawn as | Used by |
|---|---|---|---|---|---|
| 0–19 | A bullet streak | `0x8008AC30` / `0x80088674` | `0x80091198` (28), only 0–9 exist | streak (kind 1) | rifles, MGs, WA-Finger |
| 20–39 | B billboard shot | `0x8008DEC0` / `0x8008DB18` | `0x80092980` (40), ID − 20 | billboard (kind 2) | handguns, pulse, AR1000, GT2000, flamethrower, WR-RS7 |
| 40–59 | C model shot | `0x8008E664` / `0x8008E1B0` | `0x80092C20` (36), ID − 40 | 3D model (kind 0; kind 4 rigid for #6, 7, 11, 12) | snipers, bazookas, rockets, grenades, arm cannons, WX-ED2, WX-C/4 |
| 60–69 | D energy streak | `0x8008D8CC` / `0x8008D584` | `0x80092824` (24), ID − 60 | streak | laser / plasma rifles, AW-XC65 |
| 70–89 | E dispenser | `0x8008CBCC` / `0x8008C948` | `0x80092910` (28), ID − 70 | billboard | WM-X5-AA, WM-X10, WM-X15-EX |
| 90+ | F mine | `0x8008D38C` / `0x8008D0A4` | not read | rigid model | WRR-10 |

Oddities: ID 8 (WA-Finger) 5 bullets per trigger, 4 extra ammo; ID 30 (WG-HG770) flies ¼ range then re-spawns as
B #11 with 3× range; ID 52 (WX-ED2) sets +0x4E = 5 `?`; ID 66 (WG-XW11) second shot from the other side.
Shots per trigger: detail +2 bits 0–1.

| Field | A (28 B) | B (40 B) | C (36 B) | D (24 B) | E (28 B) |
|---|---|---|---|---|---|
| Speed | +8 | +0 | +0 | +0 | +0 |
| Spread mask | +10 | +2 | — | +2 | +10 / +12 |
| Width / size | +12 → +0xCA | +4 → +0xBE | +2 scale | +4 → +0xCA | — |
| Trail | +14 → +0x22 | — | — | — | +4 → +0x22 |
| Picture | +16 → +0xC8 sprite | +6 → +0xBC sprite | +6 model | +6 → +0xC8 | +14 → +0xBC |
| Colour | +24 → +0xC4 RGB | atlas colour; glow +28 RGB | texture | +12 → +0xC4 | — |
| Fire sound | +21 | +10 | +8 | +10 | +6 |
| Pellets | — | +14 | — | — | +8 |
| Flash word | +0 → +0x34 | +32 → +0x38 | +28 → +0x38 | +16 → +0x34 | +20 → +0x34 |
| Debris word | +4 → +0x38 | +36 → +0x3C | +32 → +0x3C | +20 → +0x38 | +24 → +0x38 |

B extras: +8 life factor; +12 byte 1 → normal flash, else spark burst (`0x8008B7B8`); u32 +12 bits 24–25 trailing
glow (`0x8008A700`, kind 6, sprite/size table `0x80092C00` by bits 26–31, colour +28); +16/+18 sprite animation.
Life = range ÷ speed (`0x8008A898`). Streak draw `0x800645D8` (half-width +0xCA × 40 ÷ depth, colour pool +0xC4,
additive). Billboard draw `0x80064C68`.

### Cannons (`0x80081F70`)
Recoil on the firer: player block +0x214 × cannon table +2 >> 8 (table `0x80091310`, 6 bytes: speed, recoil, `?`).

| Model | Weapons | Shot |
|---|---|---|
| 0 | WC-CN35 | streak: speed 500, sprite 7, tint `fa3200`, width 20, trail 3, sound 4 |
| 1 | WC-LN350 | B #6 |
| 2 | WC-ST120, WC-SPGUN | B #7 |
| 3 | WC-GN230 | timed shell `0x8008B1C0`, model 49, sound 0, bursts at the lead point (`0x800155DC`) |
| 4 | WC-XP4000 | B #5 |
| 5 | WC-XC8000 | D #1 |
| 6 | WC-01QL | C #3 (model 56) |
| 7 | WC-IR24 | timed shell, model 56, sound 53 |

### Missiles
See `mapping/battle-system.md` ("Missiles"). Flight record +0x20 flash word, +0x24 debris word, +0x16 blast
count. Sounds id 2 launch, id 12 flight.

## 3. Packed effect words

**Flash word** (`0x8008B60C`): bits 0–2 flash kind (7 none; table `0x800930DC`: 0 fireball, 1 hot glow, 2 spark
spray, 3 flames, 4 blue flare, 5 magenta flare, 6 orange flare); 3–4 frames per step; 5–9 length (with 13–16);
10–12 light colour (table `0x80092814`: 0 orange `ff8250`, 1 yellow-green `e6ff64`, 2 blue `4080f0`, 3 green
`40f0a0`; 7 none) through the per-area light list `0x8005B96C`; 13–16 delay; 17–26 size; 27–31 hit-sound record
(table `0x800930A0`: 0 id 6 @90, 1 id 6 @50, 2 id 22 @90, 3 id 22 @50; record 7 used by AW-DC/2, WX-C/4, AW-R/4
is past the end — likely silent `?`). Flash update `0x8008A000`.

**Debris word** (`0x8008C164`): bits 18–22 chip count, 0–9 spread, 10–17 life. Chips are triangles cut from the
target's model (`0x8008BDF8`, kind 3).

## 4. Effect sprite atlas (`0x80091A28`)

297 × 12 bytes: clut, tpage, u, v, w−1, h−1, R, G, B. Pictures in RTIM entry 61, additive.
0–7 shot shapes (4 round head with tail, 5 wide capsule, 7 diamond); 8–19 fireball; 20–31 hot glow; 32–33 sparks;
34 smoke; 35–38 lens flares; 42–49 flames; 50–278 HUD; 282/283 round glow; 284–287 purple orbs (WR-RS7, jam);
289–292 noise, sparkles, ring (pulse), sparkle dot (handguns etc.); 293–296 smoke puffs.

## 5. Impact, knockback and stun

Damage ✔: `raw = attack × (shell or energy factor) >> 6`, damage = raw × 2 ÷ 3. No fall-off.

On-hit callback `0x80082430`:
1. Rumble: damage × 128 ÷ 1000 + 128, 5 steps (`0x800174E4`).
2. Indicator: player block +0x24E = 40. AP 0 → wreck (`0x80079774`).
3. **Impact 0 = nothing else.** Every energy weapon has impact 0: energy weapons never knock back or stun.
4. **Knockback level** = impact × 256 ÷ (AC +0x16C), 0–5 (`0x80079AA4`). AC +0x16C = stability stat × 25 ÷ 4;
   stability stat (32–255) ≈ legs stability × carried weight ÷ 4,070,000 (+800 SP-ABS, +1,600 SP-ABS_Re).
   So level ≈ impact × 41 ÷ stability stat.
5. Level ≥ 1: booster rise flags (AC +0x150, +0x164) cleared `?`; push ≈ impact × 3.2 units a step, capped at 98
   (rifle 7 = measured −448 ✔); ground → state 0x80, hit animation 37–40 by side, busy flag AC +0x36 bit 0x10,
   stagger handler `0x8007FECC`, AC +0xDA = level counting down; air → state 0x81, animations 41–44, handler
   `0x8007BC1C`; cannon stance (state 34) → pushed, stays crouched (`0x8007B454`).

**Detail +0x12 bit 0x40 — lock jammer (WR-RS7 only).** AC +0x3A = attack (200), halved with a NOISE CANCELER
head (player block +0x284 bit 0 clear, `0x800796A4`). While it counts down (`0x8007CF28`) all missile locks are
wiped each step (`0x80080084`) and the gun lock is skipped (`0x8008012C`): ≈ 9.1 s jam (≈ 4.5 s with a canceler).

**Bit 0x80 — burn (WG-FGI-00 only).** AC +0x3C = 100 steps, +0x3E = damage ÷ 4; each step AP −= +0x3E
(`0x8007D038`). Total burn = 6.25 × the hit's damage over 4.5 s.

## 6. Every weapon

Reload = fire interval in steps (+hold = post-shot hold). KB level at stability 38 (default AC) / 128.

### Hand weapons
| Weapon | Type | Atk | Dmg | Impact | KB | Push | Reload | Speed | Shot | Colour | Snd | Hit |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| WG-RF35 | RIFLE | 218 | solid | 3 | 3/0 | 7 | 5 +hold | 700 | round head | `fa64fa` | 4 | hot glow 160, orange |
| WG-MGA1 | MG | 85 | solid | 0 | 0/0 | 0 | 1 | 700 | round head | `fa64fa` | 48 | sparks 160 |
| WG-MG500 | MG | 135 | solid | 0 | 0/0 | 0 | 2 | 700 | round head | `fa9600` | 48 | sparks 260 |
| WG-AR1000 | MG | 105 | solid | 0 | 0/0 | 0 | 1 | 800 | sparkle dot | atlas | 4 | hot glow 160 |
| WG-HG235 | HAND GUN ×3 | 226 | solid | 3 | 3/0 | 7 | 5 | 720 | sparkle dot | atlas | 4 | sparks 200, no light |
| WG-RF/5 | SNIPER | 795 | solid | 7 | 5/2 | 20 | 10 | 780 | model 48 | | 4 | hot glow 320 |
| WG-RF/P | SNIPER | 918 | solid | 9 | 5/2 | 26 | 12 | 840 | model 48 | | 4 | hot glow 288, green |
| WG-HG512 | HAND GUN | 437 | solid | 4 | 4/1 | 10 | 8 | 800 | sparkle dot | atlas | 4 | fireball 200 |
| WG-FG99 | FLAMETHROWER | 512 | solid | 0 | 0/0 | 0 | 1 | 88 | fireball | atlas | 53 | fireball 240 |
| WG-B2120 | BAZOOKA | 1730 | solid | 15 | 5/4 | 45 | 16 | 512 | model 54 | | 53 | fireball 280 |
| WG-B2180 | BAZOOKA | 2330 | solid | 28 | 5/5 | 87 | 22 | 576 | model 54 | | 53 | fireball 512 |
| WG-XP1000 | PULSE | 302 | energy | 0 | 0/0 | 0 | 3 | 800 | ring | atlas | 50 | hot glow 160, blue |
| WG-XP2000 | PULSE | 435 | energy | 0 | 0/0 | 0 | 6 | 840 | ring | atlas | 50 | hot glow 160, blue |
| WG-XC4 | PLASMA | 820 | energy | 0 | 0/0 | 0 | 10 | 900 | wide capsule | `60a0f0` | 51 | hot glow 140, blue |
| WG-1-KARASAWA | LASER | 1550 | energy | 0 | 0/0 | 0 | 8 | 900 | wide capsule | `60f0a0` | 51 | hot glow 256, green |
| WG-RFM118 | RIFLE | 340 | solid | 0 | 0/0 | 0 | 5 | 700 | round head | `fa64fa` | 4 | hot glow 160 |
| WG-XFwPPk | LASER | 1120 | energy | 0 | 0/0 | 0 | 10 | 900 | wide capsule | `60f0a0` | 51 | hot glow 256, green |
| WG-HG1 | HAND GUN ×3 | 280 | solid | 10 | 5/3 | 29 | 8 | 720 | sparkle dot | atlas | 4 | sparks 200 |
| WG-PB26 | GRENADE | 2200 | solid | 40 | 5/5 | 98 | 10 +hold | 512 | model 54 | | 53 | fireball 280 |
| WA-Finger | MG ×5 | 155 | solid | 0 | 0/0 | 0 | 2 | 1000 | round head | `fa64fa` | 4 | hot glow 160 |
| WG-HG770 | HAND GUN | 540 | solid | 1 | 1/0 | 1 | 7 | 720 | orange flare | `b48c50` | 4 | sparks 200 |
| WG-XW11 | LASER ×2 | 549 | energy | 0 | 0/0 | 0 | 6 | 900 | wide capsule | `808020` | 51 | hot glow 140 |
| WG-FGI-00 | FLAME SHELL | 200 | solid, burn | 0 | 0/0 | 0 | 15 | 448 | model 53 | | 53 | fireball 240 |
| WG-RF/E | SNIPER | 3800 | energy | 0 | 0/0 | 0 | 48 | 2700 | wide capsule | `32fa32` | 51 | hot glow 140 |
| WG-MG500/E | MG | 185 | energy | 0 | 0/0 | 0 | 2 | 900 | round head | `7878fa` | 48 | sparks 260 |

### Weapon arms
| Weapon | Type | Atk | Dmg | Impact | KB | Push | Reload | Speed | Shot | Snd |
|---|---|---|---|---|---|---|---|---|---|---|
| AW-MG25/2 | MG | 158 | solid | 0 | 0/0 | 0 | 2 | 700 | round head `fa9600` | 48 |
| AW-GT2000 | GATLING | 305 | solid | 0 | 0/0 | 0 | 2 | 800 | sparkle dot | 4 |
| AW-RF105 | CANNON | 1530 | solid | 17 | 5/5 | 52 | 15 | 448 | model 53 | 53 |
| AW-30/3 | DUAL MISSILE | 830 | solid | 12 | 5/3 | 36 | 10 | 80 / 350 | missile | 2+12 |
| AW-RF120 | CANNON | 2120 | solid | 22 | 5/5 | 68 | 18 | 448 | model 53 | 53 |
| AW-S60/2 | DUAL MISSILE | 830 | solid | 12 | 5/3 | 36 | 10 | 80 / 350 | missile | 2+12 |
| AW-XC5500 | PLASMA CANNON | 1241 | energy | 0 | 0/0 | 0 | 7 | 544 | model 56 | 53 |
| AW-XC65 | LASER CANNON | 2322 | energy | 0 | 0/0 | 0 | 10 | 900 | smoke `ff5a1e` | 51 |
| AW-DC/2 | GRENADE | 3822 | solid | 20 | 5/5 | 61 | 60 | 600 | model 49 | 0 |
| AW-R/4 | CANNON | 1024 | solid | 5 | 5/1 | 13 | 15 | 576 | model 54 | 53 |

### Back weapons
| Weapon | Type | Atk | Dmg | Impact | KB | Push | Reload | Speed | Shot | Snd |
|---|---|---|---|---|---|---|---|---|---|---|
| WM-S40/1, S40/2, S60/4, S60/6 | SMALL MISSILE | 830 | solid | 12 | 5/3 | 36 | 10 | 80 / 350 | missile | 2+12 |
| WM-MVG404, MVG802 | MISSILE | 1560 | solid | 23 | 5/5 | 71 | 10 | 70 / 320 | missile | 2+12 |
| WM-L201 | LARGE MISSILE | 4300 | solid | 64 | 5/5 | 98 | 10 | 60 / 300 | missile | 2+12 |
| WM-X201 | MULTI MISSILE | 980 | solid | 14 | 5/4 | 42 | 15 | 60 / 300 | missile | 2+12 |
| WM-X5-AA | BOMB ×8 | 675 | solid | 10 | 5/3 | 29 | 50 | 70 | bomb | 4 |
| WM-X10 | BOMB ×16 | 675 | solid | 10 | 5/3 | 29 | 50 | 70 | bomb | 4 |
| WM-P4001 | DUAL MISSILE | 830 | solid | 12 | 5/3 | 36 | 10 | 80 / 350 | missile | 2+12 |
| WM-PS-2 | TRIPLE MISSILE | 830 | solid | 12 | 5/3 | 36 | 10 | 80 / 350 | missile | 2+12 |
| WM-AT | LARGE MISSILE | 9830 | solid | 64 | 5/5 | 98 | 35 | 30 / 100 | missile | 2+12 |
| WM-TO100 | SMALL MISSILE | 230 | solid | 5 | 5/1 | 13 | 5 | 80 / 350 | missile | 2+12 |
| WM-SMSS24 | MISSILE | 1630 | solid | 30 | 5/5 | 93 | 10 | 60 / 300 | missile | 2+12 |
| WM-MVG812 | SMALL MISSILE | 830 | solid | 12 | 5/3 | 36 | 30 | 30 / 100 | missile | 2+12 |
| WM-X15-EX | PLASMA DISP. ×4 | 1560 | energy | 10 | 5/3 | 29 | 15 | 120 | bomb | 4 |
| WR-S50 | SMALL ROCKET | 1310 | solid | 19 | 5/5 | 58 | 8 | 448 | model 53 | 53 |
| WR-S100 | SMALL ROCKET | 1310 | solid | 19 | 5/5 | 58 | 12 | 448 | model 53 | 53 |
| WR-M50 | ROCKET | 2240 | solid | 33 | 5/5 | 98 | 12 | 512 | model 54 | 53 |
| WR-M70 | ROCKET | 2240 | solid | 33 | 5/5 | 98 | 16 | 512 | model 54 | 53 |
| WR-L24 | LARGE ROCKET | 3980 | solid | 59 | 5/5 | 98 | 16 | 576 | model 54 | 53 |
| WR-RS7 | SPECIAL ROCKET | 200 | solid, jammer | 59 | 5/5 | 98 | 40 | 512 | purple orbs | 53 |
| WRR-10 | MINE | 2240 | solid | 59 | 5/5 | 98 | 8 | | rigid model | 53 |
| WC-CN35 | CHAIN GUN | 338 | solid | 0 | 0/0 | 0 | 2 | 500 | diamond `fa3200` | 4 |
| WC-ST120 | SLUG ×7 | 183 | solid | 1 | 1/0 | 1 | 22 | 700 | sparkle dot | 4 |
| WC-LN350 | LINEAR | 690 | solid | 3 | 3/0 | 7 | 6 | 720 | sparkle dot | 4 |
| WC-GN230 | GRENADE | 3520 | solid | 52 | 5/5 | 98 | 32 +hold | 600 | model 49 | 0 |
| WC-XP4000 | PULSE CANNON | 770 | energy | 0 | 0/0 | 0 | 5 | 840 | ring | 50 |
| WC-XC8000 | PLASMA CANNON | 2065 | energy | 0 | 0/0 | 0 | 10 | 900 | wide capsule `60f0a0` | 51 |
| WC-01QL | LASER CANNON | 1531 | energy | 0 | 0/0 | 0 | 7 | 544 | model 56 | 53 |
| WC-SPGUN | SLUG ×7 | 208 | energy | 2 | 2/0 | 4 | 8 | 700 | sparkle dot | 4 |
| WC-IR24 | LASER CANNON | 3025 | energy | 48 | 5/5 | 98 | 40 +hold | 800 | model 56 | 53 |
| WX-S800/2 | DUAL MISSILE | 1120 | solid | 16 | 5/5 | 49 | 12 +hold | 80 / 350 | missile | 2+12 |
| WX-S800-GF | DUAL MISSILE | 1120 | solid | 16 | 5/5 | 49 | 10 +hold | 70 / 320 | missile | 2+12 |
| XCS-9900 | MULTI MISSILE | 980 | solid | 14 | 5/4 | 42 | 15 +hold | 60 / 300 | missile | 2+12 |
| WX-ED2 | PLASMA CANNON | 572 | energy | 0 | 0/0 | 0 | 6 +hold | 384 | model 60 | 0 |
| WX-C/4 | LASER CANNON | 1259 | energy | 0 | 0/0 | 0 | 32 +hold | 600 | model 65 | 0 |

Missile speed = thrust / top speed.

## 7. Fire sounds
4 rifle shot (rifles, handguns, snipers, slug/linear, chain gun, dispensers); 48 machine gun; 50 pulse; 51 laser;
53 bazookas, rockets, grenades, flame, mines, arm cannons; 0 cannon shot (WC-GN230, WX-ED2, WX-C/4, AW-DC/2);
2 / 12 missiles. Hit sound id 6 for almost everything.

## 8. Open items
- Live checks: stun length per knockback level; burn total; jam time with/without a Noise Canceler.
- Timed shells' draw kind; dispenser and mine fields (`0x8008D0A4`); B +16/+18; A +18–+23.
- Arena / missions engines: same tables at other addresses (pattern match only).
