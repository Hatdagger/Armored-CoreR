# Android build

Roadmap step 9. The same game as on the PC, as an app for Android tablets
and phones (arm64, Android 5.0 or newer), played with a controller. Touch
controls come later.

## Getting the APK

GitHub builds it on every push: open the repo's **Actions** tab, pick the
newest **Build** run on the branch you want, and download
**armored-corer-android** at the bottom of the page (a zip with
`armored-corer-android.apk` in it). You need to be signed in to GitHub to
download it.

## Installing and first start

1. On the tablet, open the zip (Files app) and tap `armored-corer-android.apk`.
2. Android asks to allow installing apps from this source (the browser or
   the Files app): allow it, go back, tap the APK again, **Install**.
3. Have your Master of Arena disc image (USA, SLUS-01030) on the tablet:
   the **.bin** file (the .cue isn't needed). Downloads is a good place.
4. Plug in the controller, start **Armored-CoreR**.
5. The first screen asks for the disc: press **A** (or tap the screen),
   pick the .bin in Android's file picker. The import takes a minute or
   two; it is kept inside the app (about 1 GB) with the saves, so the .bin
   can be deleted afterwards.

A newer APK installs over the old one and keeps the import and the saves
(each build is signed with the same test key, `android/test.keystore`).
Uninstalling the app deletes them.

## Importing the disc again

**SYSTEM > SETTINGS > RE-IMPORT DISC** (Android and PC). It opens the file
picker. Pick the Master of Arena .bin, or Project Phantasma's, and it is
imported over the old import. Saves, Arena and Rogue Arena progress and the
controller layout are kept. The game then closes. Start it again to load the
new import. Use it when a change says "re-import the disc" (for example the
cannon kick from the feel pass).

## Updating from inside the app

**SYSTEM > SETTINGS > UPDATES** (only in the Android app). Opening the page
checks GitHub; if there is a newer build, pick **DOWNLOAD AND INSTALL** and
confirm on Android's own install screen. The import and the saves are kept.

- The first time, Android sends you to "Install unknown apps" for
  Armored-CoreR: switch it on, come back, press A again.
- Builds come from the **android-latest** release
  (`https://github.com/Hatdagger/Armored-CoreR/releases/tag/android-latest`),
  which every push to `main` replaces with its APK and a `version.txt`
  (build number, commit, date). A release needs no GitHub sign-in, so the
  same page can be opened in the tablet's browser to install by hand.
- Builds from other branches (pull requests) are only in the Actions tab.
  If one of those is installed and has a higher build number, the page says
  it is newer than the release and offers nothing.
- The app's code for it: `android/app/src/main/java/.../Updater.java`
  (download straight into an Android install session) and
  `src/platform/sdl3/update.c` (the page, over JNI).

## How it works

- **Renderer**: Android has OpenGL ES, not desktop OpenGL, so
  `src/render/acr_render_gles.c` draws the same pictures as the PC's
  `acr_render_gl.c` with one small shader standing in for OpenGL 2.1's
  fixed-function steps (vertex colour x texture, the PS1's double-bright
  texels, alpha test, fog). It asks for OpenGL ES 3.0 and falls back to
  2.0. On a PC, `cmake -DACR_RENDER_GLES=ON` builds the game with it for
  testing (checked with the real import: the menus and fights look the
  same as the OpenGL 2.1 build).
- **Import**: the disc screen opens the system's file picker
  (`src/platform/sdl3/disc_pick.c`; on the PC too: A or Enter). Android
  hands back a `content://` link, not a file name; the game opens it and
  gives the importer `/proc/self/fd/N`, so the 700 MB image is read where
  it is instead of being copied. A .cue can't be used on Android (it only
  names its .bin, which the app may not open), so the screen asks for the
  .bin.
- **Storage**: import, saves, Arena and Rogue Arena progress and the
  controller layout all go in the app's own folder (`SDL_GetPrefPath`).
- **Build**: `android/` is a Gradle project around the normal
  `CMakeLists.txt` (the game becomes `libmain.so`, SDL `libSDL3.so`); SDL's
  Java side comes from the same SDL release. The **android** job in
  `.github/workflows/build.yml` builds it with GitHub's Android SDK and NDK.

Building it yourself: install the Android SDK and NDK, clone SDL
`release-3.2.10`, copy its `android-project/gradle` and `gradlew` into
`android/`, then `cd android && ./gradlew assembleRelease -PsdlDir=PATH_TO_SDL`.

## Not yet

- Touch controls (controller only for now).
- Only arm64 devices (all current tablets and phones).
- Not tried on a device by us: the cloud can't run Android. Reports from
  the tablet decide what gets fixed next.
