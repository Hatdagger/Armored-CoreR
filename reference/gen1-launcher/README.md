# Launcher

The launcher is the framework's own launcher window (recomp-ui) with this
project's Master of Arena style menu drawn over it.

- `moa_menu.inc`: the menu (START GAME, SETTINGS, MODS, UPDATES, EXIT), the
  START GAME page, the automatic disc finder and the first-run defaults. Included by recomp-ui's
  `launcher_imgui.cpp` through the hook in `patches/recomp-ui/`.
- `acg_starter.c`: "Armored Core Gen 1.exe" (Windows), see Folder layout.
- `moa_update.h/.cpp`: the updater. Release channel: GitHub Releases, checked
  against `SHA256SUMS.txt`. Dev channel (compiled out of `public` builds): the
  newest successful "Release builds" run on `main`, downloaded with the
  player's own GitHub token.

Brought over from the AC1Recomp fork's launcher (the user's own design) on
2026-11-20, trimmed to this ladder step. Rogue Arena, Extra Garage, Extras and
the custom network pages come back with their own steps.

## The recomp-ui patch

recomp-ui has no hook for a title's own menu. `CMakeLists.txt` applies
`patches/recomp-ui/*.patch` to the submodule's working tree at configure time
(skipped when already applied), so a local checkout shows recomp-ui as
modified after configuring. After bumping recomp-ui, if the configure stops
with "recomp-ui patch does not apply", redo the four small edits the patch
makes and regenerate it with `git -C recomp-ui diff > patches/recomp-ui/0001-title-menu-hook.patch`.

## Settings

Every player setting is on the menu's own SETTINGS page, on four tabs
(DISPLAY, PICTURE, SOUND / SYSTEM, ADVANCED; L1 / R1 or Q / E to switch).
Each row in `kSettings` is either a launcher-model call (as on the stock page)
or one of the framework's player settings (`psx.runtime.settings`, from
`psxrecomp/recompiler/src/player_settings.h`), read and saved through the mod
provider. Rows whose setting this build doesn't have are hidden. Where the
model and the framework both own a setting, only the model row is listed.
Raw tuning values (post-effect chain, PGXP tolerance, line limits), JogCon,
mouse and hotkeys stay on the stock page, reached from ADVANCED > STOCK
SETTINGS PAGE. Framework settings can only change once the disc is verified.

## Menu sounds

`moa_ui_sound.*` plays the game's own menu sounds (cursor, OK, back, window
opens, refused), read from the player's disc when the launcher has one:
`GG/MS/MENU_VAB.T` entries 0 + 1, program 0, note 60, as the game plays them
(`docs/research/sound-map.md` section 5). `moa_vab.h` decodes the PS1 sound
bank. Nothing is shipped: no disc, no sounds. The disc is read in the
background through the framework's own reader (`PS1::ISOReader`). They follow
VOLUME and can be turned off with SETTINGS > SOUND / SYSTEM > MENU SOUNDS
(saved as `menu-sounds.txt` in the player's settings folder, `%APPDATA%\Armored-Core-Gen-1\` on Windows).

## Disc finding

On the first menu draw with no usable disc, the launcher looks in the player's
folder (Windows layout, below), then beside the game, and in `disc`, `discs`,
`iso`, `roms` and `games` under each, for an image whose SYSTEM.CNF names
SLUS-01030 (any file name), using the framework's own disc check
(`disc_identity.h`).

## START GAME

START GAME opens a page with three ways in:

- **LOAD GAME**: a list of the Master of Arena saves on memory card 1
  (`BASLUS-01030A` … `O`, read from the card file the framework hands the
  launcher, checked as the game's LOAD checks them). Picking one starts the
  game straight on the hub with that save loaded.
- **NEW GAME**: straight to the game's name entry.
- **LAUNCH GAME**: the title screen. FULL BOOT (left / right, saved as
  `full-boot.txt` in the player's settings folder) plays the logos and intro;
  off (default) skips them.

The launcher hands the choice over with `acg_start_set()` (`game/acg_start.h`)
just before the game starts; `game/acg_quick_start.c` acts on it when the game
would open its title (a function override on `0x800AAE94`). See that file for
what each road replays. RESUME GAME (a game running) skips the page.

## VERSUS and ROGUE ARENA

Listed ahead of their ladder steps so the menu has its final shape: VERSUS
opens a page with LOCAL LINK (0.4), NETWORK (0.6), PLAYER VS AI and AI VS AI
(0.7); ROGUE ARENA (0.8) sits on the main menu. Each says what it will be and
which version brings it; choosing one only shows "not ready yet". When a mode
lands, its row gets a real start (`kVersusModes` / `MAIN_ROGUE` in
`moa_menu.inc`).

## Folder layout (Windows)

The Windows zip is laid out for the player
(`scripts/package_prebuilt_release.sh`, checked by the release workflow):

    Armored Core Gen 1.exe   starts the game (acg_starter.c)
    README.txt
    Memory Card/             card1.mcd, card2.mcd (and the game's options state)
    Mods/                    drop folder for new .psxmod packages
    RecompData/              the game: exe, DLLs, assets, bios, mods/, settings

- `acg_starter.c` creates `Memory Card/` and `Mods/` if missing and starts
  `RecompData/<game>.exe --memcard-dir "<folder>\Memory Card"` (the framework's
  own option), passing any other arguments through. A `--memcard-dir` already on
  the command line wins (for running two copies later, Link Versus).
- The launcher notices it runs from a folder named `RecompData` and treats the
  folder above as the player's: discs are looked for there first, and the MODS
  page's drop folder is `Mods/` (installing copies a package into the game's
  own `RecompData/mods/installed/`; the framework keeps its mods there).
- The updater updates the player's folder from a zip with the same layout and
  never touches `Memory Card/`, `Mods/`, discs, or the player's files inside
  `RecompData/`. A flat build won't install a RecompData zip (and the other way
  round): unzip that one by hand.
- Starting `RecompData/<game>.exe` directly still works, but memory cards then
  live in `RecompData/`.
- Linux and macOS keep the flat folder for now.

## Japanese disc

The Japanese disc (SLPS-01855) has its own program, built with `-DACG_REGION=JP` and shipped
in `RecompData/Japan/` (see `regional/README.md`). It is the same launcher with
`ACG_REGION_JP=1`: the menu says MASTER OF ARENA JAPAN, START GAME offers only LAUNCH GAME
(normal boot; LOAD/NEW wait for the Japanese addresses), and the disc finder takes either
disc, its own first. Picking the other version's disc makes the framework close this
program and start the other one. The updater treats `RecompData/Japan/` as part of the
game folder: it updates the program and keeps the player's files there.
