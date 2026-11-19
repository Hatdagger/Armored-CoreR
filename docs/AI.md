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

## Gen 1 brain, first version (2026-11-19)

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

**Next**: the game's own opponent AI (arena engine FDAT 210: per frame
`0x8008D4AC`, setup `0x80055768`, the scoring table at
`0x80098890-0x80098A70`) is not mapped yet. Once it is, a "MoA" brain
can sit beside this one, fed by the same record, and each arena
opponent's build and record from `MENU_TIM.T`.
