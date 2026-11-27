# Research carried over from the recomp repos

Everything learned about Master of Arena while working on the AC1Recomp fork and Armored-Core-Gen-1, copied here
on 2026-11-26 before those repos were deleted. Notes only: addresses, formats and rules. No game code or data.
All addresses are for the **US disc 1 (SLUS-01030)**.

Where a note names a source file (`moa_card.c`, `moa_arena_run.c`, `mod_builtin_moa_*.c` …) it means the old
AC1Recomp fork at `7884f4c` (0.8.4.4.2). The ones worth keeping are in [`reference/`](../../../reference/README.md).

| Note | What it covers |
|---|---|
| `disc-versions.md` | The discs on hand (US, Japan, Phantasma) and how they differ |
| `mapping/README.md` | Index of the mapping lanes, plus arena rosters and save format pointers |
| `mapping/battle-system.md` | Fights: player block, AC object, damage, missiles, lock-on |
| `mapping/stat-block.md` | The 324-byte compiled stat block, every byte |
| `mapping/parts-tables.md` | Part tables, weapon detail records, options |
| `mapping/boot-loads.md` | What loads at each step: boot, menus, hub, VS, arena |
| `mapping/mode-launch.md` | Mode byte, resume flags, starting Scenario / VS directly |
| `mapping/menus-hub.md` | The hub ring and how to change its entries |
| `mapping/menus-screens.md` | Garage, Shop, System, Arena, Options, VS menu |
| `mapping/missions.md` | Mission launch, MIS.T, per-mission code, Phantasma mission tests |
| `mapping/renderer.md` | How a fight frame is drawn; plan for in-between frames and widescreen |
| `mapping/arena-prizes.md` | Arena prize parts |
| `mapping/ram-map-gameshark.md`, `ram-map-codes.md` | RAM addresses from cheat codes |
| `mapping/weapon-map.md`, `mapping/sound.md` | Full weapon and sound lanes: every shot, look, sound, impact and stun |
| `weapon-map.md`, `sound-map.md` | Shorter versions of the two above |
| `ram-map.md`, `ram-map.toml` | Named RAM addresses (the `.toml` is the source list) |
| `mapping/scratchpad.md` | The game's scratchpad memory use |
| `jp-port.md` | Matching addresses on the Japanese disc (SLPS-01855) |
| `custom-parts/` | OP-WLINK custom option part (on hold) |
| `phantasma/missions-import.md`, `netplay-notes.md` | How the old fork did the Phantasma import and link netplay |

| `features/rogue-arena.md` | Rogue Arena as built in the old fork |
| `features/netplay-room.md` | The netplay room: lobby, ready-up, practice, hold-START menu |
| `features/last-release-notes.md`, `features/gen1-pull-requests.md` | What the last releases and pull requests changed |

Tools that read your own disc for this research are in `tools/research/recomp/`.
