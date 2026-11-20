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

In the game: **NETWORK** on the main menu.

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
- After the match, Esc (or Back on a controller) returns to the main menu.

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

- **Targets**: your FCS only ever locks on enemies (rules from the user):
  - the enemy you're on stays your target while it's in the sight, even
    when another enemy crosses in front of or behind it;
  - starting a lock with two enemies in the sight side by side: the closer;
  - **switch target**: tap Back / Select (keyboard X) for the next enemy in
    the sight, near to far and round again. With only one gun or missile
    (the blade aside), weapon switch (Triangle / Y, Tab) does it too;
  - with no enemy in the sight: the nearest enemy (its AP at the top, the
    red dot on the radar). A new target drops the locks.
- **Leaving online**: Back + Start on a controller (Back alone switches
  target now), Esc on the keyboard.
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

## Over the internet: mission briefings

No server of ours. NETWORK > HOST ONLINE shows player 1 a
**mission briefing**; player 2 picks JOIN WITH A MISSION BRIEFING and
enters it. The briefing is a connection code (`src/net/acr_brief.h`):

> MISSION BRIEFING: Destroy Cobalt Reach underground fortress squatters.
> Payout 48,000 C. Time 03:40. Wingman Vesper.

- **What's in it:** player 1's internet address and port (32 + 16 bits)
  and a 16-bit check, scrambled, written as 6 words (lists of 64, 64,
  128, 128, 128, 128), payout 1,000-99,000 C, time 00:00-23:59 and a
  wingman (256 names): about 65 bits. The word lists are data
  (`src/net/acr_brief_words.c`): any words, but keep the list sizes, the
  alphabetical order and no word twice (`test_brief` checks).
- **Finding the address:** the link asks public STUN servers (Google,
  Cloudflare, Nextcloud; `ACR_STUN=host:port` overrides) what address its
  socket shows to the internet. The same PC and port give the same
  briefing every time.
- **Step 1, one code:** player 2 sends straight to player 1's address. If
  player 1's router lets it in (many do; always when UDP port 7777 is
  forwarded), the match starts.
- **Step 2, the read-back:** after 5 seconds without an answer, player 2's
  screen flashes URGENT MISSION REQUEST, then shows a **READ-BACK** (player
  2's own address, the same kind of code). Player 1 presses A on the
  briefing screen and enters it; player 1 then sends to player 2 five
  times a second ("punching"), which opens player 1's router for player
  2's packets. Player 2 keeps trying for 5 minutes.
- **Entering a code:** each field is a drop-down list (A opens it, up/down
  scrolls, LB/RB or left/right pages, A picks); on a keyboard, typing
  jumps in the list. Either code can always be copied (X on a pad, C or
  Ctrl+C) and pasted (Ctrl+V, or the PASTE row for pads). A typo fails the
  check ("code not recognised"); a read-back entered where a briefing
  belongs is spotted and named.
- **What still can't connect:** two routers that both change the port for
  every destination (some mobile and carrier-grade NAT). Then: LAN, or
  direct IP with port 7777 forwarded (player 1's screen shows the address),
  or later a relay on a player-run server.
- **Testing on one PC:** `--host-online`, `--join-brief "TEXT"`,
  `--readback "TEXT"` (player 1 enters it at once), `--guest-port N`;
  `ACR_NET_STRICT=1` makes player 1 act like a strict router (nothing gets
  in from an address it hasn't sent to), so step 2 can be seen working.
  Both copies log their code ("netplay: MISSION BRIEFING: ...").

Not the AC1Recomp library (recomp-net): its rollback is built for
recompiled games whose state is a whole PlayStation's memory. Our fight
state is small, so a plain rollback is simpler to trust. Internet codes
use plain STUN and hole punching on the match's own socket instead of
libjuice / full ICE: short codes only have room for one address.

## Next

- Co-op: the Arena's rankers as the AI ACs once the Arena work lands; a
  friendly-fire switch if wanted.
- Online: UPnP port opening (makes step 1 work more often), clickable
  `acr://` links for codes.
- Lockstep mode for LAN, spectators, more players (host-run model), Xbox
  System Link.
- Replays: the pads of a match are all it takes (REC on the HUD).
