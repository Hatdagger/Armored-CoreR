/* Project Phantasma's arena (49 opponents + the player's place), from the
 * Phantasma disc (USA, SLUS-00670): Armored-CoreR's main Arena.
 *
 * Where it lives (ACG1PLUS docs/research/mapping/content-data.md section 4,
 * ai.md section 3; checked on the owner's disc 2026-11-26):
 *  - GG/MS/MENU_TIM.T entries 232-281 description 1, 282-330 emblems (TIM),
 *    332-381 portraits, 382 the builds (50 x 64 bytes, Master of Arena's
 *    layout), 432-481 description 2, 482 the names (24-byte slots ended by
 *    '>': 49 pilots, then 49 ACs), 483 the rewards (8 bytes a rank: u32
 *    money, u16 prize id 0-8 or 0xFFFF).
 *  - The AI settings are not with the opponents: the arena engine (FDAT 205)
 *    keeps 49 records of 60 bytes at +0x453C8 (RAM 0x8008E498). Record =
 *    rank - 1 is a guess (the research hasn't proven it).
 *  - Part numbers are Master of Arena's, except back weapons: Phantasma
 *    has no WM-MVG812 / WM-X15-EX pair at 16-17 (its 16 is a DUMMY), so
 *    Phantasma back 17 and up is Master of Arena's + 1 (17 -> 18 ...).
 *  - Prize ids 0-8 are Phantasma's own nine prizes; they are written as
 *    Armored-CoreR prize ids 23-31 (acr_gen1_arena.c).
 *
 * Writes <out_dir>/09-phantasma/<RR>.toml (arena = 9) and <RR>-emblem.tim,
 * the same files acr_arena_moa.c writes for Master of Arena's arenas. */
#ifndef ACR_ARENA_PP_H
#define ACR_ARENA_PP_H

#include <stddef.h>
#include <stdint.h>

#define ACR_ARENA_PP_NUMBER 9           /* Armored-CoreR's number for it (acr_gen1_arena.h) */
#define ACR_ARENA_PP_FOLDER "09-phantasma"
#define ACR_ARENA_PP_PRIZE_BASE 23      /* Phantasma prize id p -> Armored-CoreR prize 23 + p */

/* `pp_unpacked`: the Phantasma import's unpacked/ folder (GG/MS/MENU_TIM/NNN.bin,
 * GG/COM/FDAT/205.bin). 0 with a message in `err` on failure; `count` gets
 * the opponents written. */
int acr_export_pp_arena(const char *pp_unpacked, const char *out_dir, uint32_t *count, char *err, size_t err_size);

#endif
