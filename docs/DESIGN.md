# Design notes

The full plan lives in the
[design doc](https://claude.ai/artifact/PAAEo6cvjUi7qxFQqSGPrf). This file keeps
the decisions the code depends on.

## Target hardware: the original Xbox

Everything is sized so it could run on an original Xbox later. That keeps the
game light on PCs and handhelds too.

| Budget | Size | Use |
| --- | --- | --- |
| Game data | 32 MB | parts, ACs, stages, missions, simulation |
| Textures | 24 MB | all graphics |
| Audio | 8 MB | sound effects, voice, music buffers |
| Total | 64 MB | the Xbox's whole memory |

`src/core/acr_pools.c` enforces these: allocations past a pool's budget fail.

## Language and portability

- **C11**, no compiler extensions, so the code also builds with
  [nxdk](https://github.com/XboxDev/nxdk) for the Xbox.
- The core never calls the operating system. Windowing, input, time, files and
  audio go through `src/platform/`.

## Generations

Gen 1, 2 and 3 each get a **ruleset** (how ACs move, how energy, heat and stats
work) and an **importer** (reads that generation's discs). In a crossover, every
AC keeps its own generation's ruleset, so its quirks carry over.

## Enemy AI

AI flies an AC through the same commands a controller gives, sees the battle
only through a perception view, and is split into profile, tactics and brain
layers so later generations and modes can add their own. See [AI.md](AI.md).

## Deterministic simulation

- Game logic runs at a **fixed tick**. Rendering interpolates between ticks, so
  the feel never depends on the framerate.
- Gameplay math uses **20.12 fixed point** (`src/core/acr_fixed.h`), the
  precision the PS1 itself used. The same inputs give the same results on every
  PC and on the Xbox, which online play and replays depend on.
- Gen 1 runs **22.0 logic steps a second** (one step per 715 NTSC scanlines),
  measured from Master of Arena: [research/gen1-timing.md](research/gen1-timing.md).
  The engine draws as fast as the screen allows and interpolates between steps.

## Replays

A replay is the battle's **inputs**, not a recording of what happened. Because
the simulation is deterministic, playing the inputs back through it rebuilds the
battle exactly. Online play sends the same inputs, so replays come with it.

- **What a replay holds:**
  - a header: engine and ruleset versions, stage, the battle's random seed;
  - each AC's full build (parts, colours, Human PLUS, generation), and its AI
    profile if a computer flies it;
  - one command word per AC per tick: the same commands a controller or the AI
    gives (see Enemy AI), run-length encoded because commands change rarely.
- **Size:** at 22 ticks a second and a 16-bit command per AC, a raw minute is
  about 5 KB for two ACs and far less after run-length encoding, so replays
  stay tiny even inside the Xbox budget.
- **Nothing outside the simulation goes in.** The camera, HUD and sound are
  rebuilt during playback, so a replay can be watched from any camera (the
  spectator and replay cameras are the same code).
- **The random seed lives in the simulation state.** Gameplay never asks the
  platform for random numbers, or playback would diverge.
- **Checksums:** every few seconds the replay stores a checksum of the
  simulation state. Playback compares them, so a mismatch names the tick where
  two runs diverged. The same check catches online desyncs.
- **Version locked:** a replay plays only on the engine and ruleset versions in
  its header. Anything else is refused with a clear message rather than
  replaying wrongly.
- **Tests:** recorded replays with their expected checksums go in `tests/`, so
  any change that alters gameplay shows up as a failing replay.

## Connecting players

There are no official servers. Everything works without a server; private
servers run by players are optional extras.

- **System Link (LAN) is the main multiplayer path on the Xbox.** Games find
  each other by broadcasting on the local network, so there are no codes to
  type: open lobbies, ongoing missions open to invasion and King of the
  Mountain arenas just appear in a list. PC builds use the same LAN protocol,
  so an Xbox and a PC on one network can play together when their
  simulation versions match.
- **Over the internet (PC):** peer-to-peer, using public STUN servers to
  learn each side's internet address (no server of ours). Ways to connect:
  1. **Mission briefing (one code):** the host's screen shows a mission
     briefing (see below) holding its address. The joiner enters it and
     connects straight away whenever the host's router lets them in.
  2. **Read-back (second code, only when needed):** if nothing comes back
     within 5 seconds, the joiner's screen flashes URGENT MISSION REQUEST
     and shows a read-back (the joiner's address, same format). The host
     enters it and both sides send to each other, so their routers let
     the match through.
  3. **Clickable links (later):** every code can also be shared as an
     `acr://` link (for example in Discord); clicking it opens the game and
     fills in the code.
  4. **Direct IP:** the host forwards a port on their router once, and
     joiners enter the host's IP and port on an on-screen number pad. Only
     the host's router has to cooperate, so this also works for joiners
     behind strict routers. A host behind carrier-grade NAT cannot forward
     ports and needs someone else to host.
  UPnP port mapping (later) would make step 1 work for more hosts.
  Players whose routers block all direct connections join through a private
  server (below), which relays for them.
- **Private servers:** the game has a **dedicated server mode** (the same
  program without graphics) that anyone can run on a **PC** (Windows or
  Linux, including small boards like a Raspberry Pi) or a **modded Xbox**.
  Servers are community-run, never required, and added once by IP as a
  favourite. A server can provide:
  - a lobby list and short room codes for its players;
  - relaying for players who can't connect directly;
  - invasion alerts over the internet (it knows who is in an invadable
    mission);
  - permanent King of the Mountain and survival arenas that it hosts itself,
    so a round never ends because a player quits;
  - hosting spectators, taking that load off the players.

  Server mode runs the **same simulation and handshake** as a player. It
  joins the session as a participant **without an AC**, like a spectator, so
  it never uses one of the 9 player slots. Without rendering, the
  simulation and relaying fit easily in the Xbox's CPU and 64 MB.
- **Briefing codes:** 64 bits (IPv4 address 32, port 16, check 16),
  scrambled so neighbouring addresses look unrelated, written as a mission
  briefing: "[Action] [Place] [Place] [area] [structure] [target]. Payout
  [XX,000] C. Time [HH:MM]. Wingman [name]." Example: *"Destroy Cobalt
  Reach underground fortress squatters. Payout 48,000 C. Time 03:40.
  Wingman Vesper."* The fields are a mixed-radix number: word lists of 64,
  64, 128, 128, 128 and 128 entries (40 bits), payout 1,000-99,000 C (99
  choices), time (1440) and wingman (256 names), about 65 bits. The
  joiner's read-back is the same format under a READ-BACK header (the
  check tells the two kinds apart). Rules for the word lists:
  - every action works with every target, and so on across the slots;
  - no word appears in two lists; one word per entry;
  - original words only, no names from the Armored Core games;
  - lists are alphabetical, because the menus show them as they are.

  Each field is picked from a drop-down list with the D-pad (typing jumps
  in the list on a keyboard), and every code can be copied and pasted
  whole. A wrong field fails the check and shows "code not recognised".
  The word lists are a data table (`src/net/acr_brief_words.c`).
- Both paths use the same messages, the same handshake and the same
  simulation; only discovery differs.

## Connection handshake

Online, each machine runs the battle itself from the shared inputs, so both must
run exactly the same simulation. Before a battle the games compare:

- the **simulation version**: a hash of the gameplay code, not just a build
  number;
- each generation's **ruleset version**;
- a **hash of the imported part data**, since each player imports from their
  own discs;
- the **match rules** from the lobby (stage, time limit, Human PLUS, overweight,
  banned parts, best of N);
- each player's **AC build**, validated against those rules on both machines.

Any mismatch ends the connection with a message that says what differs (for
example "your opponent is on 0.6, you are on 0.7"). Only when everything
matches does the host send the random seed and the battle start. A replay
header carries the same fields, so a replay is checked the same way.

## Players and joining

- The simulation has **player slots** (up to **9**), never a fixed "player 1 and
  player 2". The cap is one constant; early versions may use fewer, and
  nothing in the core assumes a number. Nine fits 3v3v3 teams, a full
  King of the Mountain gauntlet and free-for-alls.
  (Master of Arena hard-wires two AC slots, which is what blocked 3-4 unit
  battles in the recomp.)
- **Joining mid-battle or mid-mission:** the host picks a future tick, sends a
  snapshot of the whole simulation state for that tick, and the new player's
  inputs count from that tick on. The connection handshake runs first, as for
  any connection.
- Snapshots are **self-contained** (IDs, no memory pointers), so the same
  snapshot serves rollback, joining, desync dumps and replay seeking.
- **Missions are written for "the players"**, not "the player": spawn points,
  objectives and scripted events take any number of player ACs. Original
  single-player missions get extra spawn points and co-op rules as data.

## Invasions

A player attacking a base can be invaded: another player gets an
**URGENT MISSION** alert and arrives mid-mission to defend it. Technically
this is a mid-mission join (above); the rest is game design and discovery.

- **Factions:** every AC, MT and turret belongs to a side, and AI targets by
  side. The defender joins the base's side, so the base's units treat them as
  an ally and the attacker as the enemy. Friendly fire is a mission rule.
- **Invadable missions are marked in mission data**, with:
  - arrival points for the defender (map edges, a drop point, a hangar) and a
    short arrival sequence;
  - defender objectives (destroy the invader, protect targets, hold until a
    timer) and how they change the attacker's result;
  - an **invasion window**: after the mission starts, before a point of no
    return such as the boss fight or the last objective.
- **The alert hides the sync:** the defender's urgent briefing takes a few
  seconds, long enough to run the handshake and transfer the snapshot, so the
  arrival feels instant.
- **Consent and fairness:** "allow invasions in my missions" and "accept
  defence alerts" settings, and alerts can be declined. **On LAN both are on
  by default; over the internet both are off by default.** Alerts only appear
  while a player is somewhere safe (garage, menus, between missions), never
  mid-battle. When several LAN players are free, all of them get the alert
  and the first to accept defends (more if the mission allows several
  defenders). The handshake checks
  the defender's build against the mission's rules. Rewards for both sides,
  and what each gets when the other leaves, are mission data.
- **Leaving and results:** if the defender leaves, the mission carries on as
  normal. If the host (the attacker, who owns the mission) leaves, it ends for
  both. Each player's result goes into their own save, from the same final
  state.
- **Cost:** an invaded mission re-simulates two players plus every AI unit
  during rollback. It must fit the per-type budgets (engine rule 8); a
  slightly higher input delay in invasions is acceptable to cut re-simulation.
- **Discovery without a server:** someone has to know which missions are open
  to invasion right now. Options, in order of effort:
  1. **LAN:** the attacker's game broadcasts "mission X in progress,
     invadable"; other games on the network show the alert.
  2. **Friends:** games that have connected before remember each other and can
     show "your friend's mission is open", joined with a fresh code.
  3. **Private servers:** a community server lists the invadable missions
     of its players and sends alerts over the internet.

## Survival arena

A 1v1 that never ends: when an AC is defeated, the next challenger enters,
until the player is defeated or extracts.

- **Entrants come from a challenger queue** in arena data: AI profiles, or
  open slots that players join through the normal mid-battle join. AI
  entrants are spawned by the simulation, no network needed.
- **Damage, ammo and AP carry over** between entrants, with an optional small
  repair per kill (a rule in the data).
- **Extraction:** an extract zone, or an extract option after each kill. The
  payout grows with every AC beaten and is lost on defeat.
- **Entry points** are chosen far from the surviving AC.

## King of the Mountain

Teams of one to three hold the arena; challengers join as a team or solo.
Winners stay, losers are eliminated, and the next challengers come in.

- **Teams are factions** (engine rule 9). Up to three teams in the arena at
  once fits the 9-slot cap.
- **The champions host.** If the challengers win, the host sends a snapshot
  to the new champions and hosting moves to them. If the host quits mid-round,
  that round ends.
- **Rules data:** repair between rounds (none, partial, full), a reign limit,
  streak rewards, and whether challengers see the champions' builds.
- **The queue waits as spectators** and sees the round they are about to
  join.
- Found through System Link like any lobby; over the internet, the champions
  share a code.

## Spectating

Every mode, single player included, can be watched.

- **A spectator is a live replay:** it receives the snapshot plus everyone's
  inputs and runs the simulation itself. It never sends inputs, so it adds
  nothing to rollback or input delay.
- **A few seconds of delay** stops spectators feeding information to players
  and means a spectator never has to roll back.
- **Bandwidth is tiny** (inputs only), and a spectator can relay the stream to
  other spectators so the host doesn't serve everyone.
- **Cameras:** follow a player, free camera, overhead. The same cameras work
  in replays.

## Engine rules

Cheap to follow from the start, expensive to retrofit:

1. **IDs, not pointers,** in the simulation state (AC 3, bullet 120).
2. **Fixed iteration order** everywhere in the simulation: no loops over hash
   maps or anything whose order can differ between machines.
3. **Gameplay content is data with stable IDs:** parts, stages, missions and AI
   profiles live in versioned tables, not code. Their hash feeds the
   connection handshake.
4. **Saves and settings carry a version** and an upgrade step from older
   formats, so a new build never breaks an old save.
5. **All on-screen text comes from tables** (translations, controller-specific
   button prompts).
6. **The UI is resolution- and aspect-independent:** 4:3 for the Xbox, 16:9 and
   16:10 for PCs and handhelds.
7. **Debug view and desync dump:** the simulation state can be inspected live,
   and when checksums disagree both machines write their state at that tick so
   the two can be compared.
8. **Per-type budgets** (ACs, bullets, effects) inside the memory pools, so a
   large co-op battle degrades gracefully on Xbox-sized hardware.
9. **Factions:** every unit has a side and AI targets by side, never by
   "attack the player". This is what makes co-op, versus and invasions work
   with the same AI.
10. **Camera and HUD sit outside the simulation:** they only read its state.
    Spectators, replays and split views can then look at any unit from any
    angle without touching the simulation.

## Legal

- No copyrighted data ships. The importer reads the player's own discs.
- Engine code is written new. The recomp is used to observe how the original
  behaves, not copied.
