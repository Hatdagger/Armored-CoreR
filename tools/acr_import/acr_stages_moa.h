#ifndef ACR_STAGES_MOA_H
#define ACR_STAGES_MOA_H

#include <stddef.h>
#include <stdint.h>

/* Master of Arena stages (mission maps and arenas) as readable files (docs/research/gen1-stages.md):
 *
 *   <out>/<NN>/stage.toml      name, start points, area count
 *   <out>/<NN>/visual.obj/.mtl the stage's flat-coloured models (FDAT 5s+4 chunk 4),
 *                              placed by the area records (chunk 8)
 *   <out>/<NN>/collision.obj   the collision polygons (chunk 7), placed the same way
 *   <out>/<NN>/sky.obj, far.obj chunk 3's sky sphere / far plane around the origin
 *
 * NN = stage number: 0-19 are the mission maps, 20-40 the arena / VS
 * stages (Green Silo is 27). Both OBJ files are in the
 * engine's world, in world units: x = -(game x), y up = -(game y),
 * z = game z (the game's world is mirrored against the engine's: its
 * facing is (-sin, cos)). Reads <unpacked>/GG/COM/FDAT/NNN.bin. Returns 0
 * with a message in `err` on failure; stages whose file is missing are
 * skipped. */
int acr_export_moa_stages(const char *unpacked_dir, const char *out_dir, uint32_t *stages,
                          char *err, size_t err_size);

/* Project Phantasma's 17 mission maps (2026-11-27): its FDAT 5m+4 (no
 * first chunk) and RTIM m, from <pp>/unpacked, written as stages
 * ACR_PP_STAGE_BASE + m (50-66). Lit with Master of Arena's light sets. */
#define ACR_PP_STAGE_BASE 50u
#define ACR_PP_STAGE_MAPS 17u
int acr_export_pp_mission_stages(const char *pp_unpacked_dir, const char *moa_unpacked_dir, const char *out_dir,
                                 uint32_t *stages, char *err, size_t err_size);

/* The colour a stage face is lit with (docs/research/gen1-stages.md, "Stage
 * lights"): the GTE's NCCS (sf = 1, lm = 1) on the model normal `n` (4096 =
 * 1) with a light set's light matrix `llm` (turned for an area of `quarter`
 * quarter turns, the area draw's way), colour matrix `lcm` and back colour
 * `bk`, and the colour 0x404040. Each channel 0-255; 128 = the texel as it is. */
void acr_moa_light_face(const int16_t llm[9], const int16_t lcm[9], const uint8_t bk[3], unsigned quarter,
                        const int16_t n[3], uint8_t out[3]);

/* Name of a stage number (the arena's stage list), or NULL. */
const char *acr_moa_stage_name(unsigned stage);

/* The VRAM a stage loads before its file (RTIM <stage>: 8-bit TIMs), for
 * exporters that texture other models of the stage (mission units).
 * tpage / clut are a TMD face's tsb (page) and cba (palette) words. */
typedef struct acr_moa_vram acr_moa_vram;
acr_moa_vram *acr_moa_vram_load(const char *unpacked_dir, unsigned stage);   /* NULL: none */
void acr_moa_vram_free(acr_moa_vram *v);
int acr_moa_vram_has_page(const acr_moa_vram *v, unsigned tpage);
/* 1 if the palette has see-through colours (bit 15): semi faces blend. */
int acr_moa_vram_semi(const acr_moa_vram *v, unsigned tpage, unsigned clut);
/* The 15-bit colour (with bit 15) a face sees at texel (u, t) of its page. */
unsigned acr_moa_vram_texel(const acr_moa_vram *v, unsigned tpage, unsigned clut, unsigned u, unsigned t);
/* The page (256 x 256 texels) with its palette as an indexed PNG. */
int acr_moa_vram_write_page(const char *path, const acr_moa_vram *v, unsigned tpage, unsigned clut);

#endif
