/* A small renderer shaped like the hardware the game targets: textured,
 * vertex-coloured triangles with depth, fog and blending, plus a HUD layer.
 * Lighting is not done here: the engine lights vertices on the CPU the way
 * the PS1 did (acr_light.h), so every platform shows the same picture and a
 * back end only has to draw. The PC back end is OpenGL 2.1 (fixed
 * function), close to what the original Xbox offers.
 *
 * Coordinates: the 3D view uses the engine's world (y up). The HUD is laid
 * out on a 480-high grid whose width follows the screen (640 at 4:3, ~853
 * at 16:9), drawn at full resolution. */
#ifndef ACR_RENDER_H
#define ACR_RENDER_H

#include <stdint.h>

typedef struct acr_vtx {
    float x, y, z;
    uint8_t r, g, b, a;
    float u, v;
} acr_vtx;

typedef struct acr_fog {
    float start, end;          /* distance from the camera; end <= start = no fog */
    uint8_t r, g, b;
} acr_fog;

typedef void *(*acr_getproc)(const char *name);

/* Loads the GL functions it needs. 0 on failure. */
int  acr_render_init(acr_getproc getproc);

/* Start a frame at the drawable's pixel size; clears it. */
void acr_render_begin(int pixel_w, int pixel_h, uint8_t r, uint8_t g, uint8_t b);

/* 3D view in a pixel rectangle (origin top left): column-major 4x4
 * projection and view matrices. Clears depth inside it. */
void acr_render_view3d(int x, int y, int w, int h, const float proj[16], const float view[16], const acr_fog *fog);
void acr_render_tris(const acr_vtx *v, int count, uint32_t texture);
void acr_render_lines(const acr_vtx *v, int count);

/* Model triangles the PS1 way: with ACR_MESH_PS1_TEXEL a vertex colour of
 * 128 shows the texel as it is (texel x colour / 128, up to twice as
 * bright); see-through texels (alpha 0) are skipped. ACR_MESH_SEMI blends
 * without writing depth (draw these after the solid ones). */
enum { ACR_MESH_PS1_TEXEL = 1, ACR_MESH_SEMI = 2, ACR_MESH_OVERLAY = 4, ACR_MESH_ADD = 8 };
/* ACR_MESH_OVERLAY: faces lying on top of others in the same plane (a
 * model's second layer, which the PS1 simply drew later) are pulled toward
 * the eye in depth so they always show over the face below instead of
 * flickering through it.
 * ACR_MESH_ADD: the PS1's additive blend (screen + colour x alpha) for
 * glows, fire and sparks; no depth writes and no fog (fog would add the
 * fog colour where the sprite is see-through). */
void acr_render_mesh(const acr_vtx *v, int count, uint32_t texture, int flags);
/* The stage's sky: textured faces drawn PS1-style around the eye (moved
 * with it), behind everything else (no depth, no fog). Call right after
 * acr_render_view3d, before the stage. */
void acr_render_sky(const acr_vtx *v, int count, uint32_t texture, const float eye[3]);

/* HUD over a pixel rectangle; returns the HUD grid width for that rectangle
 * (the height is always 480). */
float acr_render_hud(int x, int y, int w, int h);
void  acr_hud_rect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int filled);
void  acr_hud_line(float x0, float y0, float x1, float y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void  acr_hud_text(float x, float y, const char *text, uint8_t r, uint8_t g, uint8_t b);
/* The same font at `size` HUD units a glyph (acr_hud_text is 8), see-through by `a`. */
void  acr_hud_text_size(float x, float y, const char *text, float size, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
/* Part of a texture on the HUD: texels (u, v, tw, th) of a tex_w x tex_h
 * texture drawn into (x, y, w, h), tinted by the colour (white = as is).
 * Blend as the PS1 does: ACR_HUD_ALPHA the texture's alpha (a = colour
 * alpha), ACR_HUD_ADD screen + colour, ACR_HUD_HALF half screen + half
 * colour. texture 0 = a filled box in that blend. */
enum { ACR_HUD_ALPHA = 0, ACR_HUD_ADD = 1, ACR_HUD_HALF = 2 };
void  acr_hud_sprite(uint32_t texture, int tex_w, int tex_h, float u, float v, float tw, float th,
                     float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int blend);
/* The same, turned by quarter turns (1 anticlockwise, 2 half, 3 clockwise). */
void  acr_hud_sprite_turned(uint32_t texture, int tex_w, int tex_h, float u, float v, float tw, float th,
                            float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                            int blend, int turns);

/* Textures: RGBA8 pixels. `nearest` 1 keeps the sharp PS1 look; 2 is that
 * up close with mipmaps further off (no shimmer in the distance). */
uint32_t acr_render_texture(const uint8_t *rgba, int w, int h, int nearest);

/* Read the frame back (RGBA, bottom row first), for tests and screenshots. */
void acr_render_read(int w, int h, uint8_t *rgba);

/* Matrix helpers (column-major). */
void acr_mat_perspective(float out[16], float fov_y_rad, float aspect, float near_z, float far_z);
void acr_mat_look(float out[16], const float eye[3], const float fwd[3], const float up[3]);

#endif
