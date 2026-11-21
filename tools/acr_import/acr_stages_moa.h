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

/* Name of a stage number (the arena's stage list), or NULL. */
const char *acr_moa_stage_name(unsigned stage);

#endif
