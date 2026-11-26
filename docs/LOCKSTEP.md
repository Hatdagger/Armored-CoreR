# Lockstep netplay (LAN, 2 to 4 players)

The second online mode on the list (user, 2026-11-19: rollback first, then
lockstep for LAN, then a host-run model for bigger matches). Rollback:
[NETPLAY.md](NETPLAY.md).

## What it is

Every machine runs a step only once it has **every** player's controls for
it. So all of them always show the same fight, and nothing is ever
corrected afterwards. To keep that from waiting on the network, your own
controls take effect a few steps after you press them (the **delay**): by
then they have reached the others.

| | Rollback | Lockstep |
|---|---|---|
| Your own controls | at once | after the delay |
| The other ACs | guessed, corrected a moment later | always exact |
| A late packet | a small correction | a short pause |
| Players | 2 | 2 to 4 |
| Cost | snapshots, steps run again | none (fits the Xbox) |
| Best for | the internet, 1v1 | LAN, 2v2, System Link later |

## Where it stands (2026-11-25)

**In:** the engine part, `src/net/acr_lockstep.{h,c}`, with its tests
(`tests/test_lockstep.c`). No sockets or clocks in it, like the rollback
code, so it goes to the Xbox's System Link as it is.

- 2 to 4 machines, one player each: player 1 is the host and every packet
  goes through it, so a guest needs only the one connection.
- Lost and late packets cost time, never the match: each packet repeats
  what the other machine hasn't confirmed.
- The machine running ahead holds back a step now and then, so nobody sits
  waiting at the edge.
- A desync check every 60 steps; the host is told which machine differs.
- A machine that stalls (a dragged window, a slow disk) makes the others
  wait; they carry on together when it is back.
- The pads of each step can be handed to the replay recorder as they run.

**Not in yet (needs the network menu and the main loop, so it waits for
the open PRs):** choosing the mode on the HOST screen, the 3 and 4 player
lobby (every guest connecting to the host), "WAITING FOR PLAYER 2" on
screen, what happens when a player leaves (now: the match stops).

## How much delay

From the tests (60 steps a second, 3,000 steps = 50 s):

| Connection | Players | Delay | Time lost to waiting |
|---|---|---|---|
| LAN cable | 2 | 1 or 2 steps | none |
| LAN cable | 4 (2v2) | 2 | 0.3% |
| Wi-Fi (12 ms, jitter, 1% loss) | 2 | 3 | none |
| Wi-Fi | 4 (2v2) | 4 | 3% |
| 40 ms, 2% loss | 2 | 6 | none |
| 40 ms, 2% loss | 4 (2v2) | 8 | 1% |
| 40 ms with a delay of 1 | 2 | 1 | runs at under half speed, still right |

A step is 1/60 s, so a delay of 2 is 33 ms on your own controls. With
guests on Wi-Fi a guest's controls cross the network twice (to the host,
then on to the other guests), which is why 2v2 wants a step or two more.

## Next

- The HOST screen's mode choice (rollback / lockstep) and delay, sent in
  WELCOME; a lobby that takes up to 3 guests.
- 2v2 versus and Rogue Arena co-op for 3 or 4 once the lobby is in.
- Carrying on when a guest leaves (a computer pilot takes the AC over).
