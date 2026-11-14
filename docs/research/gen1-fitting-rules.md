# Gen 1 fitting rules (what makes an AC legal)

The game refuses to start a match with an illegal AC (both players are
checked at VS START), and the garage moves or removes parts when a new part
does not fit. The engine and the card builder must apply the same rules.

## Known so far

| Rule | Source |
| --- | --- |
| Head, core, arms, generator and FCS are required | validator `0x8007E164` (menu module) |
| Legs other than caterpillars need a booster; caterpillars carry none | validator; "Unequipped the booster." |
| Weapon arms replace the hand weapon and the blade (nothing in the hands) | "Can't equip with ARM type weapon.", "Unequipped the ARM weapon." |
| Hand weapon + blade weight must fit the core's arm load limit (not with weapon arms) | validator |
| Everything except the legs must fit the legs' carry limit | validator |
| Total energy drain must not exceed the generator's output | seen in testing: four-legged legs on the default generator were refused |
| Optional parts: total slot cost must fit the core's option slots | seen in testing; option table `0x800D1AA0` |
| Some back units take both back slots | "Moved the BACK weapons.", "Unequipped the BACK weapons." |
| Some parts can't be used with caterpillars or with certain legs | "Can't equip with a caterpillar.", "Can't equip with current legs." |
| Some radars are removed in some situations | radar indices 40, 41, 42, 44 and 46; "Unequipped the radar." |

Garage messages are at `0x80052500-0x80052680` in the menu module (FDAT 206).
The garage fitting code that clears back slots is around `0x80087EE0` and
`0x800881F0` (function `0x800865B4`), with more at `0x80083130` and
`0x80089514`.

## Back units that probably take both slots

The back table ends with six special units, the likely two-slot weapons:
52 WX-S800/2 (dual missile), 53 WX-S800-GF (dual missile), 54 XCS-9900
(multi missile), 55 WX-ED2 (plasma cannon), 56 WX-C/4 (laser cannon),
57 P77-ST (stealth). To confirm from the code.

## Gaps in the Raven Card Builder

- Handles weapon arms (clears and disables hand weapon and blade).
- Treats the two back slots independently: no two-slot back units yet.
- To check against the full rule list once decoded.

## Next

Decode the fitting routines above so every rule, with its exact condition,
comes from the game's own code; then apply them in the engine and the builder.
