# Reference: our own code from the retired recomp repos

Copied 2026-11-26 from `Hatdagger/AC1Recomp` (commit `7884f4c`, 0.8.4.4.2) and `Hatdagger/Armored-Core-Gen-1`
(commit `95ef52e`) before those repos were deleted. **Nothing here is built.** These files hooked into the
recompiled game, so they cannot run in Armored-CoreR; they are kept to read when a feature is rebuilt here.

No game code and no game data: no translated code, no disc files, no tables of values copied from a disc.

| File(s) | Why it was kept |
|---|---|
| `ac1recomp/moa_arena_run.*`, `moa_arena_run_save.inc` | Rogue Arena rules: seeding, prize offers, ghosts, run save layout |
| `ac1recomp/moa_run_record.*` | How finished runs and defeated ghosts were recorded |
| `ac1recomp/moa_vs_ai.*` | AI Battle: the 7 sliders, advanced page and the five presets |
| `ac1recomp/moa_card.*`, `moa_card_font.h` | Raven card picture format (colour strip code, emblem, paint from emblem colours) |
| `ac1recomp/moa_screen.*`, `moa_parts.*` | Extra Garage screens: Assembly layout, part details, same-slot compare panels |
| `ac1recomp/moa_chime.*`, `moa_sfx.*`, `moa_ui_sound.*`, `moa_vs_sound.*` | Our made-from-scratch sounds (90s early-CD style): menu ticks, stings, VS shots |
| `ac1recomp/moa_music.*` | Player-supplied music (MP3 / OGG) hooks |
| `ac1recomp/moa_hub_rank_bg.*`, `ac1recomp/art/hub-rank-tiles/` | The belt-rank hub background tiles (our own art) and the script that packs them |
| `gen1-launcher/` | The last launcher menu (Start Game submenu, updater notes) and its UI sounds |
| `ac1recomp/LICENSE`, `CREDITS.md` | The licence and credits these files were published under |

Left behind on purpose: translated game code (`generated/`), boot executables, the Project Phantasma tables and
mission recipe, texture and screenshot pictures, the weapon stats sheet (`weapons.csv`), the address
cross-reference made from the translated code, and the menu-driving boot automation (only useful inside the recomp).
