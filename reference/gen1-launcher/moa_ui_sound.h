/* The launcher's menu sounds: the game's own menu sound effects, read from
 * the player's disc (GG/MS/MENU_VAB.T, the bank every MoA menu loads; see
 * docs/research/sound-map.md section 5). Nothing is shipped with the game:
 * with no disc there are simply no sounds.
 */
#pragma once

namespace moa_sound {

/* Tone numbers in the game's menu bank, as the game itself uses them. */
enum Sfx {
    SFX_BACK    = 0,    /* back / cancel */
    SFX_CURSOR  = 1,    /* cursor moves */
    SFX_OK      = 2,    /* confirm */
    SFX_WINDOW  = 6,    /* a window opens */
    SFX_REFUSED = 7,    /* message box; not allowed */
};

/* The disc image to read the sounds from (any format the framework reads).
 * Loads in the background the first time a path is given; later calls with
 * the same path do nothing. */
void set_disc(const char* path);

/* Plays a sound at `volume` percent (the launcher's VOLUME setting). Does
 * nothing until the bank is loaded, or when menu sounds are off. */
void play(int sfx, int volume);

bool ready();          /* the bank is loaded */
bool enabled();        /* the player's MENU SOUNDS choice (on by default) */
void set_enabled(bool on);

} /* namespace moa_sound */
