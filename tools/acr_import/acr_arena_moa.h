/* Master of Arena's arena opponents, for the arena modes (Rogue Arena first).
 *
 * Every arena (the main arena and the Ex Arenas) keeps its opponents in
 * GG/MS/MENU_TIM.T. Arena a's entries start at 140 + 4 * start[a] + 4 * a
 * (count and start are bytes at 0x800D5A08 / 0x800D5A14 in the menu module,
 * FDAT 206): count descriptions, emblems, portraits and second descriptions,
 * then the whole arena's builds (64 bytes each), names (pilot then AC, 24
 * bytes each), rewards (8) and AI records (36). The last opponent of each
 * arena is the player's own place. (AC1Recomp docs/mapping/arena-prizes.md
 * and games/master-of-arena/moa_arena_run.c.)
 *
 * Writes <out_dir>/<AA>-<ARENA>/<RR>.toml, one file per opponent, RR = its
 * rank (01 = the arena's top), and <out_dir>/arenas.toml listing the arenas. */
#ifndef ACR_ARENA_MOA_H
#define ACR_ARENA_MOA_H

#include <stddef.h>
#include <stdint.h>

/* `menu_module`: unpacked GG/COM/FDAT/206.bin; `unpacked_dir`: the import's
 * unpacked/ folder (GG/MS/MENU_TIM/NNN.bin). Returns 0 with a message in
 * `err` on failure; `count` gets the opponents written. */
int acr_export_moa_arena(const char *menu_module, const char *unpacked_dir, const char *out_dir,
                         uint32_t *count, char *err, size_t err_size);

#endif
