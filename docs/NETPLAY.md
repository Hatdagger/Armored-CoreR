# Netplay

1v1, or two players in co-op against 1 or 2 AI ACs, over the network,
with **rollback** (as modern fighting games do).
Started 2026-11-19, branch `gen1-netplay`. Priority agreed with the user:
rollback first (1v1 and co-op, where precision matters), then delay-based
lockstep for LAN, then a host-runs-the-game model for bigger matches.

## Playing

Both PCs need the same disc imported (the game checks and refuses the
match with a message if the part, animation or stage files differ). Each
player flies their own garage build.

In the game: **GARAGE > NETWORK**.

- **HOST A MATCH** (pick the mode, AI pilot, stage and input delay first
  with left / right): waits for player 2 and shows this PC's address.
  - **MODE**: VS (the two of you against each other), CO-OP against 1 AI,
    or CO-OP against 2 AI.
  - **AI PILOT**: the AI's style in co-op (BALANCED, AGGRESSOR, SNIPER,
    DUELIST, EVASIVE: the AC1Recomp AI card presets).
- **GAMES ON YOUR NETWORK**: hosts on the same network (or another copy on
  this PC, shown as THIS PC) appear by themselves; pick one to join.
  DIFFERENT DATA means that host imported other disc data; CO-OP marks a
  co-op host.
- **JOIN BY ADDRESS**: for a host that isn't listed. With a controller,
  left / right and A pick the characters (DEL, JOIN at the end); with a
  keyboard, type it and press Enter.
- After the match, Esc (or Back on a controller) returns to the garage.

Hosts announce themselves once a second by UDP broadcast on port 7778
(and to this PC itself); the match uses port 7777.

Or skip the menu: on Windows, next to `armored-corer.exe`:

- **`host-match.bat`** on one PC: hosts as player 1 and shows the address
  to give the other player.
- **`host-coop.bat`** instead hosts co-op against 2 AI (add `--coop 1` for
  one, `--ai sniper` for another AI style).
- **`join-match.bat`** on the other: asks for that address (just Enter for
  two copies on the same PC).

The first time, Windows may ask whether Armored-CoreR may use the network:
allow it on private networks.

From a terminal instead:

```
armored-corer --host                  (port 7777; --host 8000 for another)
armored-corer --join 192.168.1.20     (or 192.168.1.20:8000; 127.0.0.1 = this PC)
```

Host options: `--stage 27` (Green Silo, the default), `--stage 39` (Ocean
Platform), `--stage 0` (open test floor); `--net-delay N` input delay in
steps (default 1); `--coop 1` or `--coop 2` co-op against that many AI,
with `--ai balanced|aggressor|sniper|duelist|evasive`.

Testing a bad connection (each side's own sending, so use it on both):
`--net-lag 50` (ms each way), `--net-jitter 20`, `--net-loss 5` (percent).

On screen: each PC shows its own player full screen. Rounds are two
minutes; the next one starts by itself 5 seconds after a round ends (the
round count and wins show under the result). F1 shows the connection line
(ping, input delay, how many corrections rollback made and the deepest).
Messages: WAITING FOR THE OTHER PLAYER, THE OTHER PLAYER LEFT, CONNECTION
LOST, and OUT OF STEP (DESYNC) if the two PCs ever disagree. During netplay
the test keys that change the fight (parts, AI, stage, step rate, reset)
are off. Esc quits.

Two copies on one PC: only the window you clicked last reads the keyboard.

### Co-op

You (player 1, the host) and player 2 are one team; the AI ACs the other.
For now the AI ACs fly copies of your builds (the first AI player 1's, the
second player 2's), in dark grey and red; the Arena's rankers come later.
The round is the usual two minutes: a team loses when all its ACs are down
(or out of the area); at time up the team with more AP left in total wins.

- **Targets**: your FCS only ever locks on enemies. Lock-on picks the enemy
  in the sight nearest its middle (holding the one you have while it stays
  in the sight); outside the sight, the nearest enemy (its AP at the top,
  the red dot on the radar). A new target drops the locks.
- **Friendly fire is on**: shots, missiles and the blade hit a teammate in
  the way. Missiles keep the enemy they were fired at.
- **On screen**: your teammate's name over their AC; on the radar your
  target is red, other enemies orange, your teammate light blue.

## How it works

- `src/gen1/acr_gen1_match.{h,c}`: the fight plus the round around it
  (clock, knock-out, result, next round) in one struct that is copied
  whole: the snapshot. Integer only; a field-by-field hash checks two
  machines agree.
- `src/net/acr_rollback.{h,c}`: no sockets or clocks (ports to the Xbox's
  System Link later). Each step: our pad now, the other player's pad if it
  came, else a guess (their last pad, one-step presses left out). A late
  pad that differs from the guess: load the snapshot before it, run the
  steps again. Pads are 2 bytes; every packet repeats the pads the other
  side hasn't confirmed, so lost packets cost nothing. At most 12 steps
  past the other player's last pad (then wait); the PC running ahead skips
  a step now and then (frame advantage). Every 60 steps, once both
  players' pads up to there are final, both sides send a hash of the
  match; a mismatch is shown.
- `src/platform/sdl3/net_link.{h,c}`: UDP (Windows and Linux), the
  connect handshake (HELLO / WELCOME / REFUSE / READY), the data check
  (hash of the import's `parts`, `motion` and the stage's folder, line
  endings ignored), pings, and the fake lag / jitter / loss for testing.
- Co-op: the battle takes up to 4 ACs (`battle.count`, per fighter `team`
  and `target`, picked every step by `acr_gen1_battle_retarget`; count 0
  is the old 1v1). The AI pilots run **inside** `acr_gen1_match_step`
  (`rules.ai_mask`, `rules.ai[]`, `rules.ai_seed` from the host's seed),
  so both PCs run identical AI and rollback replays it; only the two
  players' pads go over the network. WELCOME carries the mode and AI
  style (protocol version 2).
- `tests/test_rollback.c`: a snapshot replays exactly; whole matches over
  simulated networks (LAN; 40 ms + jitter, 2% loss; 90 ms, 5%; 120 ms,
  20% loss) end identical on both sides and identical to a plain run of
  the same controls; different data on one side is caught at the first
  check. The same for co-op against 2 AI and 1 AI.
- `tests/test_gen1_battle.c`: 3- and 4-AC fights (targets are enemies,
  friendly fire, missiles keep their target, the blade seeks enemies,
  results by team).

Not the AC1Recomp library (recomp-net): its rollback is built for
recompiled games whose state is a whole PlayStation's memory. Our fight
state is small, so a plain rollback is simpler to trust. recomp-net's LAN
discovery and ICE connection codes are still the plan for the next steps.

## Next

- Co-op: the Arena's rankers as the AI ACs once the Arena work lands; a
  friendly-fire switch if wanted.
- Lockstep mode for LAN, then copy-paste connection codes for online
  (libjuice, as the user leans), spectators, more players (host-run
  model), Xbox System Link.
- Replays: the pads of a match are all it takes (REC on the HUD).
