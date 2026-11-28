# Changelog

Changes big enough to need a way back. Newest first.

## 2026-11-28: splash damage

**Why.** ACG1PLUS read the blast rule off the Phantasma disc (`docs/research/mapping/weapon-effects.md` 2c,
PR #41 there); the owner asked for it here, and corrected that the shooter can be caught too.

**Old behaviour.** Model shots and timed shells only hurt what they hit directly.

**New behaviour.** WG-PB26, AW-DC/2, WC-GN230 and WC-IR24 end in the big blast (15 ticks, out to 1,262);
WG-B2180, WR-L24, AW-XC5500 and WC-01QL in the medium one (10 ticks, out to 575), on an AC, a wall or the
floor (a timed shell also when its fuse runs out). The blast grows as a shell; each AC or unit whose middle
it passes takes one hit, the shooter included: attack / 8 and impact / 4 for model shots (the whole record
for timed shells, still to be checked live), from 100 % at the centre to about 75 % at the edge. Blade waves
have no blast. Needs the re-import from the earlier passes (the shots' explosion numbers).

**Files.** `src/gen1/acr_gen1_battle.{c,h}`, `src/platform/sdl3/demo_data.c`, `src/net/acr_replay.h`
(rules 11 -> 12), `tests/test_gen1_battle.c`, `tests/test_replay.c`.

**How to switch back.** `git revert` the PR's merge commit, or clear `blast_on` in `demo_data.c`.

## 2026-11-28: hit size and lunge per legs, no air-slash waves, debris chips

**Why.** Follow-up to the blade pass below (user, 2026-11-28): go with ACG1PLUS's hit size and
per-legs lunge, drop Human PLUS waves from air slashes, keep MoA's wave arming (boost held from
before the swing still sends the wave) and MoA's air slash x 3; add debris chips.

**Old behaviour.** Every AC a 75 x 212 cylinder; one measured lunge for every body; air slashes
could send Human PLUS waves; hits cut no chips.

**New behaviour.**
- Hit cylinder (shots, blade lunge contact, air-slash aim point, bodies): two legs 100 x 225, four
  legs and caterpillars 112 x 150 (ACG1PLUS mapping/blade.md 6).
- Ground lunge: the first three ticks come from the body's own swing animation (56) root motion to
  frame 7 (default 375, light legs 390 with the hop, reverse joints 187, four legs / caterpillars
  281); the push after it is shared. No track: the measured table.
- Air slashes send no Human PLUS wave (LS-1000W's wave, the whole attack, stays).
- Debris chips: hits cut the weapon's chip count off the AC (debris word from the disc: guns 4-8,
  missiles 10, bombs 16, AW-XC65 31; blade hits 10, waves 20), tumbling triangles in the AC's
  paint that fall and fade. Grey for an unpainted player 1. Needs a **re-import** for the guns'
  counts (blade and wave chips work without one).

**Files.** `src/gen1/acr_gen1_battle.{c,h}`; `src/platform/sdl3/fx.{c,h}`, `demo_data.c`, `main.c`;
`tools/acr_import/acr_parts_moa.c`; `src/net/acr_replay.h` (rules 10 -> 11); `tests/test_gen1_battle.c`,
`tests/test_replay.c`.

**How to switch back.** `git revert` the PR's merge commit (rules back to 10: `0x7F1ABFAB`,
`0xFD43D177`). Hit size alone: `ac_radius` / `ac_height` in `acr_gen1_battle.c` to 75 / 212.

## 2026-11-28: blade and projectile pass from ACG1PLUS's Phantasma mapping

**Why.** ACG1PLUS mapped the blade (`docs/research/mapping/blade.md`, branch `mapping/blade`) and the
weapon effects (`docs/research/mapping/weapon-effects.md`, branch `mapping/weapon-effects`) on
Phantasma and listed where ACR differs. Only rules that hold on **both** discs were taken (fight rules
stay MoA's); the Phantasma-only blade differences are listed below as not done.

**Old behaviour.** A ground swing kept and moved by the AC's own speed (boosting into a slash slid on);
forward held did not lengthen the lunge; the swing stopped turning after 7 ticks; locks were only
dropped when the swing ended; the seek cone had no up/down limit; an air slash cost no energy with
boost held. Blade hits drew the energy-shot flash; the beam glowed smoothly. Model shots ended in the
flash word only; timed shells looked like any model shot; no light in flight.

**New behaviour.**
- Blade (sim): a ground swing drops the AC's own velocity (stops dead, stands after); forward held keeps
  the push for five ticks (about 695 in all, 516 without); the AC turns toward its target all through
  the swing; the press wipes every lock (and the end still does); the seek cone is +-45 degrees up /
  down around the view tilt as well as sideways; an air slash with boost held drains as a rise.
- Looks: blade hits are a 7-flash spark burst; the beam strobes on and off every tick (ground ticks
  0-20, air 0-10) inside the existing lit window and keeps the per-part colours; model shots with an
  explosion model end in a large (record 0) or medium (record 5) fireball, others in their white
  flare; timed shells (WC-GN230, WC-IR24) burst into three fireballs three ticks apart, also when the
  fuse runs out; lit shots (rockets, bazookas, WC-01QL / AW-XC5500, timed shells) glow in flight.
  The new looks need a **re-import** of the disc; older imports draw as before.

**Not done (decide / test first).** Phantasma-only: air slash attack x 4 (MoA x 3, kept), no Human
PLUS wave from an air slash, wave disarmed by boost held before the hit window. Both discs but
risky without a live test: two line tests a tick in the air slash (up to 4 hits), hit cylinder
mines / HG770 split /
dispenser falling bombs.

**Files.** `src/gen1/acr_gen1_battle.{c,h}`, `acr_gen1_sim.{c,h}`; `src/platform/sdl3/fx.{c,h}`,
`demo_data.c`; `tools/acr_import/acr_parts_moa.c`; `src/net/acr_replay.h` (rules 9 -> 10);
`tests/test_gen1_battle.c`, `tests/test_replay.c`; this file.

**How to switch back.** `git revert` the PR's merge commit. The fight's part alone: revert the
`acr_gen1_battle.c` / `acr_gen1_sim.*` hunks and set `ACR_REPLAY_RULES` and the test's `GOLDEN_*`
back to rules 9 (`0x7EC43318`, `0x5A4D00D4`).

## 2026-11-28: projectiles from the disc's shot tables, Phantasma generators

**Why.** Shots didn't look like the original (one look per weapon type, by eye) and some numbers
around them were off. Details, sources and the full comparison: `docs/feel/projectiles.md`.

**Old behaviour.** Shots drawn by HUD type (rifles a purple blob, MGs a short capsule streak, every
energy shot the same blue); one shot a trigger, no spread; dispensers and five cannons at wrong
speeds; the starting generator at MoA's 6,000 output (refill 210 standing / 127 moving).

**New behaviour.** Each weapon's own picture, tint, width, streak length (trail x speed) and trailing
glow, and its impact flash; pellets (hand guns 3, slugs 7, WA-Finger 5, WG-XW11 2) and the tables'
spread masks in the fight; dispenser and redirected-cannon speeds from their own rows; GPS-VVA /
GPS-V6 / GRD-RX5 / RX6 / RX7 at Phantasma's output (refill 129 / 46). Needs a **re-import** of the
disc for the new looks and speeds; older imports draw and play as before (except the generators).

**Files.** See the changelog table in `docs/feel/projectiles.md` (one commit per change):
`tools/acr_import/acr_parts_moa.c`, `acr_hud_moa.c`; `src/gen1/acr_gen1_battle.{c,h}`;
`src/platform/sdl3/fx.{c,h}`, `demo_data.{c,h}`, `main.c`; `src/net/acr_replay.h` (rules 8 -> 9);
`tests/test_gen1_battle.c`, `tests/test_replay.c`; docs.

**How to switch back.** Revert the one commit you want gone (`git revert <commit>`), or the PR's
merge commit for all of it (`git revert -m 1 <merge>`). Reverting anything that changes fights
(pellets/spread, generators, importer speeds) should raise `ACR_REPLAY_RULES` again.

## 2026-11-28: the original Gen 1 arena AI

**Why.** Arena opponents were far too aggressive and accurate; bottom ranks beat players easily. The
brain they used was this project's own utility AI, not the disc's.

**Old behaviour.** Every computer pilot (Arena, Rogue Arena, co-op, free battle) flew the utility brain,
tuned only by six editor sliders read back from each opponent's record. It fires on nearly every chance,
aims at where you are now and never loses track.

**New behaviour.** By default every computer pilot flies the **original brain**, a port of Project
Phantasma's arena AI with Master of Arena's extra rules as a style (docs/AI.md, "The original brain"):
behaviour scoring every 8 frames, in-fight learning, a per-opponent reaction delay, the lost-track value
that rises under fast strafing, missile dodging, pad presses like a player. Each opponent runs from its
own disc record (Phantasma: record n = opponent n = starting rank − 1). Phantasma opponents fight
Phantasma-style, Master of Arena opponents MoA-style (dice to fire, slow start, 10-20 frame shot hold in
the main arena's ranks, aiming ahead, shot dodging, desperation at low AP), changeable in
SYSTEM > SETTINGS > AI PILOTS. The utility brain is kept, unchanged, as "ACR UTILITY (HARD)".

**Important.** ACG1PLUS's AI mapping, which this port follows, is a static read of the discs' code. It
has not been seen running in a live game yet. Some gaps were filled by reading Phantasma's arena code
from the project's pp-work pack (also static, marked `[read]` in the code) and some are this port's own
choices (marked `[ours]`). See docs/AI.md.

**Files.**
- New: `src/gen1/acr_gen1_ai_orig.c` (the original brain), `tests/test_gen1_ai_orig.c` (records,
  switches, determinism, ladder test), `docs/CHANGELOG.md`.
- `src/gen1/acr_gen1_ai.{h,c}`: profile gains `brain` and the disc record `rec`; the view gains what the
  original brain needs (target position, speed, facing, reach, AP, nearest missile, incoming shots, step
  rate); `acr_gen1_ai_init` / `acr_gen1_ai_think` pick the brain; the AI setting (`acr_gen1_ai_set_options`).
  The utility brain's own code is untouched.
- `src/gen1/acr_gen1_match.c`: the match hash includes the original brain's state (online desync checks).
- `src/gen1/acr_gen1_rogue.c`: a ghost's borrowed record gets the ghost's Human PLUS.
- `src/platform/sdl3/demo_data.c`: opponents' `ai_record_pp` / `ai_record` lines are read into the profile.
- `src/platform/sdl3/settings.{h,c}`, `garage.{h,c}`, `main.c`: the SETTINGS > AI PILOTS page
  (`ai_utility`, `ai_style` in settings.toml) and applying it.
- `src/net/acr_replay.h`: `ACR_REPLAY_RULES` 7 → 8 (fights with computer pilots play out differently, so
  older replays are refused rather than played wrong); `tests/test_replay.c` golden numbers updated.
- `tests/test_gen1_ai.c`: its matches now name the utility brain (they test that brain).
- `tools/acr_import/acr_arena_pp.c`: comment only (record = rank − 1 is confirmed, not a guess).
- `src/platform/xbox/Makefile`: the new source file.
- `docs/AI.md`, `docs/ARENA.md`.

**How to switch back.**
- **One setting:** SYSTEM > SETTINGS > AI PILOTS > AI BRAIN: ACR UTILITY (HARD), or `ai_utility = 1` in
  settings.toml. Every computer pilot then flies the old brain again, exactly as before.
- **Revert:** revert this change's merge commit on main (`git revert -m 1 <merge>`). The old brain alone
  is also on branch `backup/ai-utility-brain-v1`.

**To test in the morning (owner).**
1. Arena, Phantasma ladder, a bottom opponent (rank 45-49): it should be clearly beatable and slow to
   react; strafe left and right fast in front of it and watch it lose track and turn the wrong way.
2. A top Phantasma opponent (rank 1-5): still hard, fires the moment you are lined up, never loses track.
3. A middle one (rank 20-30): about even.
4. A Master of Arena main-ladder opponent: it should shoot noticeably less, especially in the first ~4 s,
   and a little late; it dodges your shots now and then; near death it goes all-out once.
5. SETTINGS > AI PILOTS: flip AI STYLE (Phantasma / Master of Arena) on the same opponent and compare
   how often it shoots in 30 s (ACG1PLUS ai.md §6 open item 4 asks for the same count on the discs).
6. Flip AI BRAIN to ACR UTILITY (HARD): the old opponents should be back, as aggressive as before.
7. Optional, on the real discs (ai.md §8): Phantasma in DuckStation, cheat `801DBA7C 0000` keeps "lost
   track" at zero; with it the AI should stop losing you when you strafe. Tell me if it doesn't.
8. Online co-op against AI: both players need the same AI PILOTS setting, or the fight desyncs.
