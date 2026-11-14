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

Plain TOML files beside the AC data, so anyone can write or tweak one:

```toml
name = "Sniper"
preset = "sniper"          # balanced, aggressor, sniper, duelist, evasive
range = 0.8                # 0 close to 1 far
aggression = 0.3
evasion = 0.7
lock_patience = 0.6
blade_use = 0.1
gun_use = 0.9
missile_use = 0.5
energy_care = 0.7          # how much boost energy it keeps in reserve
```

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

## When it is built

After the Gen 1 movement and combat rules (Phase 1), so the AI drives the
real rules from its first day. The Gen 1 brain is checked against how Master
of Arena's opponents behave, observed in the recomp, the same way movement
is checked.
