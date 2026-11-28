# Updates (SYSTEM > SETTINGS > UPDATES)

Every platform updates itself from main via a GitHub release; no sign-in needed.

| Platform | Release | Done by |
|---|---|---|
| Android | `android-latest` | `Updater.java` (see ANDROID.md) |
| Windows / Linux | `desktop-latest` | `src/platform/sdl3/update.c` |

## Desktop (Windows / Linux)

- CI builds with `-DACR_BUILD_NUMBER=<run number>`; a local build is 0, so it always sees the release as newer.
- On each push to main, the `desktop-release` job uploads
  `armored-corer-windows-x64.zip`, `armored-corer-linux-x64.tar.gz` and
  `version.txt` (build, commit, date, Windows size, Linux size; uploaded last).
- The page uses the system's `curl` (download) and `tar` (unpack). Both come with Windows 10 1803+ and normal Linux distros.
- Files are staged in `<pref>/update/new`. **RESTART AND UPDATE** writes
  `apply.bat` / `apply.sh`, starts it and quits the game. The script waits for the
  game to exit, copies the staged files over the install folder and starts the game again.
- Saves, settings and the disc import live in the pref folder and are never touched.
- The install folder must be writable. Don't put the game in Program Files.

## Reverting

To remove the desktop updater, revert its PR. With it reverted, `update_supported()` returns 0 on PC
and the menu entry does nothing, as before. You can delete the `desktop-latest` release and tag by hand.
