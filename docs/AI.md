# Enemy AI design

AI is designed so the problems from the AC1Recomp AI Battle work cannot come
back, and so Gen 2, Gen 3 and new modes can add their own behaviour later.

## Problems we are leaving behind

| In the recomp | Why it hurt | Here |
| --- | --- | --- |
| AI behaviour lived inside the game's own opponent tables | could only be borrowed from existing enemies, not authored | behaviour is its own data file, separate from the AC build |
| Matching a player's AC to an in-game AI by legs, arms and head | guesswork; a build rarely fit any AI well | any behaviour profile can fly any AC |
| Only 7 Ranker sliders reachable | little control, no presets | presets plus sliders plus an advanced page, all in one profile file |
| AI reached into game memory through hooks | broke easily and needed per-version checks | AI reads a defined view of the battle and nothing else |
| 1v1 only | no free-for-all or teams | target choice is part of the design from the start |

## The rules

1. **AI drives an AC exactly like a player does.** Each tick an AI produces
   the same command set a controller produces (move, turn, boost, jump, fire
   each weapon, lock, swap). The AC's generation ruleset then applies it.
   So an AI works with any generation's AC, and a Gen 1 AC keeps its Gen 1
   quirks whether a person or an AI is flying it.
2. **AI sees the battle only through a perception view:** positions,
   velocities, lock state, energy, heat, ammo, AP, line of sight and radar
   contacts. What it can sense depends on the AC's own parts (radar, head),
   so a blind AC is blind for the AI too.
3. **Deterministic.** AI runs on the fixed tick, uses fixed-point math and a
   random generator seeded per match. The same match replays exactly, and
   online play never desyncs because of AI.
4. **Bounded cost.** Each AI gets a fixed time budget per tick, and expensive
   thinking (path finding, target choice) is spread over several ticks, so
   8-AC free-for-alls stay smooth on low-spec machines and the Xbox.

## Layers

| Layer | Decides | Swappable per |
| --- | --- | --- |
| Profile (data) | personality: preset, sliders, preferred range, aggression, weapon habits | AC or Raven card |
| Tactics | target choice, team play, retreat or extract (survival), objective pressure (missions) | game mode |
| Brain | how the profile and tactics turn into movement and firing | generation (Gen 1 brain first; Gen 2 and Gen 3 brains later) |
| Commands | the per-tick controller-style output | shared by everything |

A new game, mode or generation adds a tactics module or a brain without
touching the others. Mission scripting (triggers, waves, objectives) is a
separate system that gives orders to tactics; it is not part of combat AI.

## Profiles

(Since 2026-11-28 the sliders feed the utility brain; the original brain runs from the disc
record, or from the record the sliders make, see below.)

Plain text files (TOML style) that anyone can write or tweak. The six
sliders are the rows of Master of Arena's own AI editor (RANKER MK AI),
0 to 100, so a profile maps straight onto the game's 36-byte AI record
(`acr_gen1_ai_record` writes it exactly as the editor does):

```toml
preset = "sniper"   # balanced, aggressor, sniper, duelist, evasive (sets all six)
shoot = 95          # how keen it is to pull the trigger
blade = 10          # how often it goes for the blade up close
lock = 95           # how long it waits for a lock (missiles: how many locks)
avoid = 50          # dodging: strafing, boosting away from locks and missiles
fly = 30            # time in the air, and the energy it is willing to spend
range = 90          # 0 close, 50 mid, 100 long
rebel = 0           # Human PLUS level 0..9 (REBEL POINT)
skill = 50          # reaction time and aim (not in the game's editor)
```

Lines after `preset` adjust it. The five presets are the AC1Recomp AI
card presets. Difficulty (`skill`) changes reaction time and aim, never the
AC's stats, so a hard opponent is smarter, not stronger.

**AI builder (planned from the start):** an in-game screen, beside the AC
garage, for making and editing profiles with sliders, the same way the recomp
launcher's AI Battle builder worked: pick a preset, adjust the sliders, save,
load or delete. It writes the same TOML file, so built and hand-written
profiles are interchangeable. More controls get added to the builder later as
the AI grows.

The five presets match the AI Battle choices from the recomp. A Raven card
without AI settings flies **Balanced**. Difficulty (for example in the
roguelike tree) scales reaction time and aim, never the AC's stats, so a hard
opponent is smarter, not stronger.

## Two brains (2026-11-28)

| Brain | What it is | Where it is used by default |
| --- | --- | --- |
| **ORIGINAL** | A port of the disc's own arena AI (Project Phantasma, with Master of Arena's extras as a style), run from each opponent's own AI record. `src/gen1/acr_gen1_ai_orig.c` | everything: Arena (Phantasma and MoA ladders, Ex Arenas), Rogue Arena ladder and co-op opponents, free battle presets, AI files |
| **ACR UTILITY (HARD)** | This project's first, custom brain (section below). `src/gen1/acr_gen1_ai.c` | nowhere, unless chosen |

**Choosing.**
- **Per fight:** `acr_gen1_ai_profile.brain` = `ACR_GEN1_BRAIN_ORIGINAL` or `ACR_GEN1_BRAIN_UTILITY`
  (a later feature such as bonus fights sets it); `ACR_GEN1_BRAIN_DEFAULT` (0, what every profile has
  today) follows the setting.
- **Everywhere at once:** SYSTEM > SETTINGS > **AI PILOTS** > AI BRAIN (`ai_utility = 1` in
  `settings.toml`). This is the **one switch back to the old behaviour**.
- **Style** of the original brain: SETTINGS > AI PILOTS > AI STYLE (`ai_style` in `settings.toml`):
  OWN DISC (default: Phantasma opponents fight Phantasma-style, Master of Arena opponents MoA-style),
  PHANTASMA for all, or MASTER OF ARENA for all. The page shows a one-line note on what differs.
- Both are read when a pilot starts a fight (`acr_gen1_ai_init`), so a change applies from the next
  fight. **Online:** both machines must have the same AI setting (the pilots run on both).

## The original brain (2026-11-28)

Source: ACG1PLUS `docs/research/mapping/ai.md` (sections given as §). **That mapping is a static read
of the discs' code: nothing in it has been seen running in a live game yet**, so this port is as good
as that reading. Where ai.md leaves a gap, this port read Phantasma's arena code itself (from the
project's pp-work pack, also static): those places are marked `[read]` in the code; choices with no
disc source at all are marked `[ours]`.

How it thinks (§1, §4), once per Gen 1 tick (the disc's frame; at higher step rates it holds its
buttons between ticks):

- **Behaviour scoring every 8 frames** (§4 chooser, phase 2): each behaviour scores
  *fit × liking × stick-with-current bonus*: fit 0..1 from the situation; liking = base weight moved
  toward the learned weight by the record's blend; the current behaviour gets 1.0 falling to 0.5 over
  its commitment time, the others 0.5; a "whim" can multiply a fit. Best score wins.
- **Behaviours ported:** 4 close in / circle (fit = average of the ground desire and how much it
  stands in front of you; it circles off your front, `[read]`), 5 hold its weapon's range and sidestep
  when you both face each other inside your reach (`[read]`; fit = the air desire), 6 hold position
  outside your reach, 7 dodge missiles (face the missile, cross its path, boost when it is near,
  `[read]`), 13 keep distance, 14 wander. Not ported: 1-3 and 12 (obstacles, stage objects, lifts,
  stage 28's drop: our arenas have no such objects; the disc itself turns 12 off on such stages).
- **Weapon choice every 8 frames** (phase 5): range against distance, learned weight, 110-frame bonus
  for the current pick; the weapon in hand loses 1 a frame, so it keeps rotating (§4, §5b).
- **In-fight learning** (§4): hit for d → current behaviour −d/4, the others +d/8, weapon in hand −d/8
  (−d for a stance cannon), the others +d/4; it hits you → current behaviour +d/4, and the weapon (or
  the blade's lunge chance) +d/4. Weights stay within 409..3686 (0.1..0.9).
- **Reaction delay** (§3 +0x11, §5 +0x028): it aims at where you were 0-30 frames ago, per opponent.
- **Lost track** (§5 +0x244, formula `[read]` at PP `0x80083650`): with tracking on, it grows each
  frame by the aim error × 0.375 (more far away) and falls by tracking/16; above zero, that share of
  frames turns the wrong way or at random. Fast strafing makes the error, so it really throws it off.
  Opponents with tracking 0 never lose track.
- **Firing** (§5b): guns when you are in sight, under 4,000 units, and both aim errors are inside a
  1.4°-2.8° cone; missiles on a lock; stance cannons: kneel, then fire on lock.
- **Blade** (`[read]`, PP `0x8008339C` / `0x80083510`): lunge inside 700 units and ±45° with the
  record's blade chance; Human PLUS opponents also swing at 1,000-5,000 units every `gate` frames.
- **Pad buttons:** it fills in the same input a pad gives (move, turn, strafe, boost, look, fire,
  blade, weapon change), never moves the AC directly (§1, §7).

**Phantasma style vs Master of Arena style** (§6; the owner's decision, §6 open item 5):

| | Phantasma | Master of Arena |
| --- | --- | --- |
| Trigger | fires whenever lined up | a dice roll on every chance (record "shoot"), a quarter of that in the first 110 frames (~4 s) |
| Shot hold | none | main arena: opponents 0-10 hold each shot 10 frames, the rest 20; Ex Arenas none |
| Aim | at your (delayed) position | ahead of you (where you will be when the shot arrives), within twice the weapon's range |
| Dodging | missiles only (behaviour 7) | also gun shots and rockets, with the record's "avoid" chance (turn 90°, boost) |
| Low AP | nothing | once, below the record's share: blade, fire and dodge to 100 %, no lost track, no delay |

**Records** (§3, §3a). Each Phantasma opponent has its own 60-byte record: **record n = opponent
number n = starting rank − 1** (confirmed in ai.md §3a; it follows the opponent, not the rank). Master
of Arena opponents have the 36-byte record its AI editor writes; the importer already copied both
into each opponent's file (`ai_record_pp = [...]` / `ai_record = [...]`) and the game now reads them
(`demo_data.c`). MoA's opponent number is taken as rank − 1 (ai.md says: its position in that arena's
list, top first). Opponents with no record (presets, AI files, ghosts without a source) get the record
the MoA editor would write from their sliders. Rogue Arena ladder opponents use the original brain
with their own records (ghosts borrow their source opponent's).

What this port could not take from the disc, and chose (`[ours]`, open for tuning): the booster
(a helper the behaviours call, not fully read: here a chance a tick, from how much a behaviour wants to
boost, to start a one-second burst while energy is over half); how far behaviour 5 holds off with a
gun (capped at 3,000, where the fire cone is widest); the turn release with our AC's turn momentum;
the pitch cone (our look steps are 41 units); MoA's behaviour-tuning layout (which editor bytes feed
which behaviour); the shot dodge's check rate and turn length; the AP term in the blade roll (left
out); which weapon a hit is credited to (the one in hand); arena walls (kept off them, as leaving the
area loses in our rules).

**Tests:** `tests/test_gen1_ai_orig.c`: the record layouts, style and brain switches, the fire hold,
determinism (the same seed replays the same match; a copy taken mid-fight, as rollback takes, carries
on the same), and a **ladder test**: a scripted baseline player (aims where the AI was 0.15 s ago
±4°, strafes side to side, rifle on 8 of 10 chances in the box, missiles at two locks when far, blade
when close) fights made-up sample opponents of ranks 49, 40, 30, 20, 10 and 1 (records shaped like
ai.md's two families, invented ACs that get stronger up the ladder: no game data in the repo) over 10
seeds. Results go to `build/ai-ladder-results.txt`. Phantasma style, first run:

| Rank | 49 | 40 | 30 | 20 | 10 | 1 |
| --- | --- | --- | --- | --- | --- | --- |
| AI wins / 10 | 0 | 0 | 1 | 5 | 7 | 7 |
| Utility brain, same fights | 1 | 9 | 10 | 10 | 10 | 10 |

It fails if rank 49 wins more than 4, rank 40 more than 5, rank 20 is outside 2-8, or rank 1 wins
fewer than 6. Fight length is logged, not tested.

## The ACR utility brain ("ACR UTILITY (HARD)"), first version (2026-11-19)

`src/gen1/acr_gen1_ai.{h,c}`. Each engine step it looks at the battle
through `acr_gen1_ai_view` (its own AC, weapons, lock state, the target's
distance and angles, missiles coming at it, the arena walls) and fills in
the same input and buttons a pad gives. It is our own utility brain, tuned
by the sliders:

- **Weapon**: scores each gun and missile by distance, ammo and sliders
  (missiles far away and against flyers; cannons only far off, unpressed,
  and it commits to the kneel for a few seconds).
- **Distance**: holds a band from RANGE (blade reach to 5,200), inside the
  weapon's reach; strafes side to side, switching sooner the higher AVOID.
- **Booster**: dodge bursts when locked on, hit or a missile is coming
  (AVOID), dashes to close a long gap, hops (FLY), always keeping an energy
  reserve (and the blade's charge if it likes the blade).
- **Trigger**: guns fire in the lock box, waiting for the lock as LOCK
  says; missiles volley at the number of locks LOCK asks for.
- **Blade**: pressed close (under 760) and ahead, as often as BLADE says.
- **Aim**: turns toward the target, letting go a tick early (turning stops
  dead on release); tilts the view with the target's height. `skill` sets
  reaction (90-350 ms between decisions) and a held aim error.
- Deterministic: its own random numbers from a per-round seed; timers in
  battle milliseconds.

`tests/test_gen1_ai.c` flies every preset against a still target (each
wins inside two minutes) and against Balanced over six rounds (both sides
take real damage; the same seed replays the same match). Results at the
time of writing: Aggressor beat Balanced 5-1, Sniper lost 1-5 (it can't
keep its range in a 20,000-wide arena against a closer), Balanced 3-3.

Since 2026-11-28 this brain is **not** the default any more (it was far too
aggressive and accurate: bottom ranks beat players easily). It is kept as it
was, selectable, for later Rogue Arena bonus fights (project doc
`claude/rogue-bonus-fights.md`), and as the one-switch way back (below).
Also saved on branch `backup/ai-utility-brain-v1`.
