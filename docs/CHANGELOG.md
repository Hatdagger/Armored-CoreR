# Changelog

Changes big enough to need a way back. Newest first.

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
