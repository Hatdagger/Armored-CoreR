# Game-feel pass (2026-11-27)

The overnight pass that brings the fight's feel to Master of Arena. Fight
rules follow MoA's disc and content stays Phantasma's. Each area doc has the
values, the disc addresses and every call that was made without the user.
**Nothing here has been play-tested.** It builds and the tests pass.

**Rollback:** the tag `pre-feel-pass` (the same commit as the branch
`rollback/pre-feel-pass`) is main as it was before the pass.

## Areas

| Area | PR | Doc | What changed |
| --- | --- | --- | --- |
| Movement | #34 | [movement.md](movement.md) | An audit found it already matched MoA. One fix: diagonal walking uses the set's own diagonal tracks (13-17 % faster than straight walking, as in the game). |
| Boost, jump, landing, energy | #35 | [boost-energy.md](boost-energy.md) | Landings split into soft (< 4,481 fall speed, 8 ticks, any move cancels it) and hard (kneel, 16 ticks, can't be cancelled). Touchdown now stops the AC dead. |
| Lock-on and FCS | #39 | [lockon.md](lockon.md) | Shots fly to the weapon's own range, not the FCS's. The lock range is a cylinder, not a sphere. Asymmetric FCS boxes are no longer turned 90°. |
| Weapons | #41 | [weapons.md](weapons.md) | Weapon switches wait out the reload. The cannon stance has a stand-up, no entry while boosting, and kick on the firing AC. Shots use the per-weapon fire sounds. |
| Blade and taking hits | #40 | [blade-hit.md](blade-hit.md) | The air slash has its own handler (homing, x3 attack). Each new hit restarts the stun (stun-lock). A stun ends the victim's swing. The swing sound plays on its frame, and blade hits flash. |
| Camera | #38 | [camera.md](camera.md) | The trailing now matches the disc's quarter-step easing, so the camera sits closer behind a moving AC. Walls and ceilings pull the camera in, as on the disc. |
| Animations | #42 | [animations.md](animations.md) | New hit flinches (by side, ground and air), the cannon kneel and stand-up, and more poses played from the disc's 57 animations. Drawing only, with no gameplay change. |
| Sounds and HUD | #43 | [sound-hud.md](sound-hud.md) | Footsteps on walk frames, plus turn, drive, jump and landing sounds. The boost is pitched as on the disc. Distance fade and voice priority are the game's. |
| Projectiles, generator (2026-11-28) | this PR | [projectiles.md](projectiles.md) | Shots drawn from the disc's shot tables (picture, tint, width, streak length, trailing glow, impact flash), pellets and spread in the fight, dispenser and redirected-cannon speeds fixed, five shared generators back to Phantasma's output. Reference screenshots in `projectiles/`. |

Smaller feel PRs merged in the same pass: #36 (the heavy bipeds' weapon-arm
jump: test and doc), #37 (fight feel 3: landing, kneel, step heights, air
slash, hit push) and #44 (the AP readout turns red at a tenth of full AP).

The replay/netplay rules version went up with these changes. Replays
recorded before the pass are refused instead of desyncing.

## Left out

All eight areas ran and were merged. No area had a `BLOCKED-*.md` file. These
items are still open inside them (details in each doc):

- The second animation layer (firing) is not drawn yet.
- The missile flight loop sound (#19) isn't hooked up.
- `cannon_kick` needs a disc re-import. Until then, every cannon uses 32.
- The two-legged drive sound is left out, and the four-legs/caterpillar step
  events aren't settled.
- Assumptions to check: hit side 39 = left, the gun "locked" beep (#10 vs
  #11), and a coast of 1-2 ticks.
- Rumble isn't modelled. The first-person view isn't touched.
