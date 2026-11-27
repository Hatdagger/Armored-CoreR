/* The hangar main menu's scenery (approved design, project doc
 * claude/hangar-menu-and-garage-ui.md, mockup A): the hangar behind the
 * the grey console with its green CRT in the foreground (the room itself
 * is 3D: hangar3d.c).
 * All drawn on the HUD grid (480 high, `hud_w` wide); the mockup's 1280x720
 * layout is scaled to 480 high and centred. The AC itself is the caller's
 * (3D, between hangar_back and hangar_fx). */
#ifndef HANGAR_SCENE_H
#define HANGAR_SCENE_H

#include <stdint.h>

/* Where the AC stands, in HUD units (the mockup's silhouette box). */
void hangar_ac_rect(float hud_w, float *x, float *y, float *w, float *h);
/* The console bezel; gives the CRT's rectangle (HUD units) and draws the
 * header line ("RAVEN NET // HANGAR", the AC's name, its COAM). */
void hangar_console(float hud_w, const char *ac_name, long coam, float *cx, float *cy, float *cw, float *ch);
/* The CRT's glass: the dark green screen (before its contents). */
void hangar_crt_back(float cx, float cy, float cw, float ch);
/* The CRT's scan sweep and scanlines (after its contents). */
void hangar_crt_front(float cx, float cy, float cw, float ch, unsigned ms);

/* Line icons on a 24x24 grid scaled to `size`: the six menu columns
 * (0 SYSTEM, 1 DATA, 2 VERSUS, 3 ARENA, 4 GARAGE, 5 MISSION) and, from 6,
 * the four sub-item marks (cycled). */
void hangar_icon(int id, float x, float y, float size, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* Filled shapes on the HUD: a quad with a colour at its top and its bottom,
 * and a soft round puff (`a` in the middle, see-through at the edge). */
void hs_quad(float x, float y, float w, float h, const uint8_t top[4], const uint8_t bottom[4]);
void hs_puff(float x, float y, float radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

#endif
