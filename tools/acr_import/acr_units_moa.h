#ifndef ACR_UNITS_MOA_H
#define ACR_UNITS_MOA_H

#include <stddef.h>
#include <stdint.h>

/* Master of Arena mission units (FDAT 5s+4 chunk 2 of mission maps 0-19:
 * the enemies, vehicles, turrets and buildings) and the objects that place
 * them (chunk 13), as readable files. Notes: docs/research/gen1-stages.md,
 * "Mission units".
 *
 *   <units>/UNN/unit.toml        where it is used, pieces, animations, skins
 *   <units>/UNN/skeleton.toml    nodes, parents, bind pose (set-NN.toml layout;
 *                                a drawing node says draws = "unit K")
 *   <units>/UNN/anims.txt        joint animations (set-NN-anims.txt layout)
 *   <units>/UNN/pieceK.obj       piece K (LOD 0), at its node's bind position (as AC parts)
 *   <units>/UNN/unit.mtl         materials: tNN = texture-NN.png, rgb-RRGGBB flat
 *   <units>/UNN/texture-NN.png   texture pages from the first map that uses it
 *   <units>/UNN/map-MM.mtl, map-MM-texture-NN.png
 *                                the same unit's paint on map MM, where it differs
 *   <stages>/MM/objects.toml     map MM's object records: unit, place, facing...
 *
 * Units are numbered U00, U01... in order of first use (map, then slot), and
 * the same model used on several maps is written once. Reads
 * <unpacked>/GG/COM/FDAT and RTIM. Returns 0 with a message in `err` on failure. */
int acr_export_moa_units(const char *unpacked_dir, const char *units_dir, const char *stages_dir,
                         uint32_t *units, char *err, size_t err_size);

#endif
