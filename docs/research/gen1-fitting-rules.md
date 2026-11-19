# Gen 1 fitting rules (what makes an AC legal)

Master of Arena applies fitting rules in two places, and the engine does the
same (`src/gen1/acr_gen1_fit.{h,c}`, tests in `tests/test_gen1_fit.c`):

- **Repair**: the garage and DATA LOAD remove parts that can't be fitted
  together (`acr_gen1_fit_repair`).
- **Check**: VS START refuses a build that breaks a limit
  (`acr_gen1_fit_check`).

All addresses are in the menu module (FDAT 206, loaded at `0x80050DD8`).
Garage parts struct `0x80036FB0`: core, booster, head, arms, blade, hand,
back L, back R, generator, FCS, (unused), legs; `0xFF` = empty.

## VS START check (validator `0x8007E164`)

| Rule | Engine failure bit |
| --- | --- |
| Head, core, arms, generator, FCS and legs are required | `MISSING_PART` |
| Legs below index 27 (all but caterpillars) need a booster | `NO_BOOSTER` |
| Optional parts' slot costs must fit the core's option slots (core `+0x36`, option `+0x28`) | `OPTION_SLOTS` |
| Generator output minus every other part's drain (legs: while moving) must be 0 or more | `ENERGY` |
| Arms + hand weapon + blade weight must fit the core's arm load limit (core `+0x32`); weapon arms count alone | `ARM_LOAD` |
| Everything but the legs (generator included) must fit the legs' carry limit (`+0x32`) | `WEIGHT` |

Human PLUS (byte `0x8003EACA`) waives the energy, arm load and weight
checks. Not modelled yet, by design; it comes later.

## Repairs (garage equip handler `0x800865B4`, DATA LOAD fix-ups)

| Rule | Game message | Seen on DATA LOAD |
| --- | --- | --- |
| No hand weapon or blade without arms | "Can't equip without an ARM part." | |
| Weapon arms (index 12+) empty the hands | "Can't equip with ARM type weapon." / "Unequipped the ARM weapon." | |
| Caterpillars (legs 27+) can't carry a booster | "Can't equip with a caterpillar." / "Unequipped the booster." | not tested (the test card writer already drops it) |
| Caterpillars can't carry radars 40, 41, 42, 44, 46 (radars 43, 45, 47 are allowed) | "Can't equip with a caterpillar." / "Unequipped the radar." | yes |
| WG-1-KARASAWA (hand 15) can't go on legs 5 (LN-SSVR), 21-24 and 26 (four legs) or 27, 28, 30-32 (caterpillars) | "Can't equip with current legs." / "Unequipped the right ARM weapon." | yes |
| ANKS-1A46J (arms 5) can't carry back weapons 0-39 or 53 (radars and other two-slot units are fine) | "Unable to equip" / "Unequipped the (left/right) BACK weapon(s)." | yes |
| Back units 52-57 take both back slots: fitting one empties the other slot | "Unequipped the left/right BACK weapon." | no (see below) |
| Fitting into one back slot the only copy of the unit in the other slot moves it | "Moved the BACK weapons." | n/a |

Two-slot units: 52 WX-S800/2, 53 WX-S800-GF, 54 XCS-9900, 55 WX-ED2,
56 WX-C/4, 57 P77-ST (the table check is index 52 or above).

Where the rules sit in the code:

- Equip handler (X pressed) checks: `0x80087514-0x800877B0`.
- Hover check that shows the "can't" marker (pointer `0x8019F3E8`), per
  slot through jump table `0x80052440`, Karasawa legs through `0x80052460`:
  `0x800863B8-0x80086588`.
- Clears after a part is fitted: weapon arms `0x80087B6C`; two-slot
  `0x80087C0C`; move `0x80087CD8`; caterpillar `0x80087D90-0x80087FA8`;
  Karasawa on leg change `0x80088050` (jump table `0x80052820`);
  ANKS-1A46J `0x80088104-0x800882A0`.
- Shared fix-ups: radars on caterpillars `0x80089514`, Karasawa
  `0x80089750`.

## Confirmed in the game

Test ACs written to a memory card, loaded with DATA LOAD, then VS START
(slot 2 card, `tools/measure`-style driver):

| Card build | Result after loading |
| --- | --- |
| Legs 6 (humanoid) + Karasawa | kept |
| Legs 5 (LN-SSVR) + Karasawa | Karasawa removed |
| Legs 27 (caterpillar) + Karasawa + radar 40 | both removed |
| Back L 52 (two-slot) + back R 0 | both kept, match starts |
| Legs 27 (caterpillar) + radar 43 | kept |
| Arms 5 (ANKS-1A46J) + back L 0 | back L removed |
| Same AC: hand + blade 1243 under the core's 2450, but arms + hand + blade 3363 over it | VS START refused (arms weight counts) |

So a card can carry a two-slot unit and a second back weapon; only the
garage prevents it. The engine keeps that on load (`fitted_to =
ACR_GEN1_SLOT_COUNT`) and clears the other slot when a two-slot unit is
fitted. What the garage does when a normal unit is fitted beside a
two-slot unit is not decoded yet; the engine clears the two-slot unit.

## Part files

`acr-import` writes each part's flags as `fit = [...]` in its `.toml`:
`arm-weapon`, `no-back-weapons` (arms); `caterpillar`, `no-heavy-hand`
(legs); `heavy-hand` (hand); `radar`, `two-slot`, `back-weapon` (back).
A modded part opts into a rule by listing the flag.

## Raven Card Builder

Handles weapon arms. Still to add: two-slot back units, caterpillar
removals, Karasawa legs, ANKS-1A46J back weapons, and the VS START check
(energy, arm load, weight, option slots).

## From the AC1Recomp mapping (2026-11-18, menus-screens.md 2h-2i)

- The game's sortie check (`0x8007E164`) checks parts, energy, arm load
  and weight, but **not** option slots or the fit rules (the garage
  enforces those). The engine keeps its option-slot check; the garage
  never lets it fail.
- New core: the options stay in bit order while they fit; the first that
  doesn't and every one after it come off (engine: `acr_gen1_garage_fit`).
- Legs that aren't caterpillars, fitted in place of caterpillars, get the
  first owned booster (engine: yes).
- Weapon arms back to regular arms restore the visit's last hand weapon
  and blade (engine: not yet, a garage-screen memory).
- Shop: buying needs credits at least the price (so nothing in debt).
  Selling out of debt is always allowed, fitted parts too (the slot
  empties). In debt, weapons and options still sell; a frame part (head,
  core, arms, legs, generator, FCS, booster) is refused if fitted or while
  the AC is incomplete, overweight or short of energy ("Pay back your debt
  first"). Engine: `acr_gen1_shop_buy` / `acr_gen1_shop_sell`.
