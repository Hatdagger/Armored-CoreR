# Gen 1 weapons, FCS and damage (first pass)

Read from Master of Arena's menu module (compile routine `0x800A7BB4`,
FDAT 206) and VS engine (FDAT 209), then checked in the recomp: the
starting AC's rifle fired at a standing default AC. Test weapons, as asked:
WG-RF35 rifle, WM-S40/1 missile, LS-2001 blade, with the COMDEX-C7 FCS.

## Weapon detail tables (24 bytes per weapon)

Separate from the shop tables (which hold only price, weight and drain):
hand weapons `0x800D1F64`, back weapons `0x800D21D4`, weapon arms
`0x800D1DE4` (index arms - 12). Weapon type names (16 bytes each, "RIFLE",
"SMALL MISSILE"...) at `0x800D5488`, indexed by byte +7.

| Byte | Meaning | WG-RF35 | WM-S40/1 |
| --- | --- | --- | --- |
| +0 | slot kind (0 hand, 1 missile, 2 cannon) | 0 | 1 |
| +6 | lock class: picks the FCS's range, lock box and lock time (4 = no lock) | 0 | 1 |
| +7 | type name | 6 RIFLE | 13 SMALL MISSILE |
| +8 | ammo | 200 | 40 |
| +0xA | energy per shot (energy weapons) | 0 | 0 |
| +0xC | ammo price | 18 | 130 |
| +0xE | weapon range | 8,500 | 9,000 |
| +0x10 | shots locked at once (missiles), capped by the FCS | 1 | 1 |
| +0x11 | fire interval byte | 5 | 10 |
| +0x12 | flags (0x10 / 0x20: shell / energy damage) | 0x17 | 0x17 |
| +0x14 | attack | 218 | 830 |
| +0x16 | impact | 3 | 12 |

## FCS (96-byte records at `0x800D11D0`)

+0x2C: most targets locked at once (COMDEX-C7: 4). Then four 12-byte
groups, one per lock class: range, lock box (outer width and height, inner
width and height), lock time.

| Class | Range | Box | Lock time |
| --- | --- | --- | --- |
| 0 (rifles, handguns, machine guns) | 6,100 | 240x240 / 200x200 | 60 |
| 1 (missiles) | 8,000 | 200x200 / 160x160 | 128 |
| 2 | 8,500 | 160x160 / 125x125 | 60 |
| 3 | 8,000 | 90x90 / 72x72 | 35 |

A weapon's working range is the smaller of its own range and its class's
FCS range (rifle: 6,100). SP-AXL scales the lock time.

## Damage (VS engine `0x80079500`, checked in game)

Each AC has a shell and an energy damage factor (compiled +0x22, +0x23):

    x = head + core + arms + legs defense (shell or energy)
        (x 1.15 with SP-S/SCR or SP-E/SCR)
    factor = (184320 - (x * 4096 - C) / 50) >> 12, limited to 1..45
    C = 3,051,520 (shell), 3,584,000 (energy)

A hit takes `((attack * factor) >> 6) * 2 / 3` AP (integer maths), shell
or energy by the weapon's flags. Default AC: shell defense 1,604 gives
factor 27, so the rifle does 60 and the missile 233, both as measured.
Setting the factor by hand gave 2, 22, 45, 58, 60, 63, 68, 102, 145 and
218 AP for factors 1 to 96, all matching. No distance fall-off.

## Measured, not yet read from code

- Rifle shots held down: one every 35-36 screen refreshes (about 13 steps).
- Rifle bullet: about 650 units a step (hit 23 refreshes later at 6,000
  than at 700).
- Missile: switched to with Triangle; first hit 157 refreshes after holding
  fire at 3,000 (lock time plus flight).
- Fire is Square.

## Still to do

Projectile speeds and missile homing from the code; the fire interval byte;
lock-on rules (box, time) in the VS engine; the blade (attack 2,050; its
other bytes 39, 738, 11 are likely reach and timing).

## In the test build

`src/gen1/acr_gen1_battle.{h,c}`: lock-on, shots, missiles, blade and
damage, with a target AC standing ahead. The damage formula and factors
are the game's; these are stand-ins until decoded or measured:

- Lock time read as 1/60 s (rifle 1 s, missile 2.1 s); lock box angle
  taken as box width over a 512-wide screen.
- Fire interval: byte x 118 ms (rifle 590 ms, as measured).
- Rifle shot 650 units a step (measured); missile 290 a step with simple
  homing (estimated); shots hit within 260 units of an AC's middle.
- Blade: reach 740, one swing a second, energy damage.
- Shot speeds are per step, like movement, so they are faster at 60 steps
  a second; lock time and fire interval are kept per second.
