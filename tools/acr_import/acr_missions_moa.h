/* Master of Arena's missions as the mission screens show them, from the
 * player's disc (AC1Recomp docs/mapping/missions.md, "Stock per-slot
 * values"; PHANTASMA-MISSIONS.md, "Mission screen"):
 *
 *   GG/MS/MIS.T entry 0   the mission table the menu copies to 0x8019F7A8 + 80:
 *                         +0 offer window (lo, hi chapter, 0, 255) x 24,
 *                         +96 names (32 bytes, '>' ends) x 24,
 *                         +960 rewards (u32) x 24, +1056 engine mission
 *                         number x 24, +1080 a byte x 24 (an enemy AC? unknown),
 *                         +1104 page records (32 bytes: u16 picture[8],
 *                         u16 text[8], each = entry - 1, -1 none) x 24
 *   GG/MS/MIS.T entries   briefing pages (ASCII, '>' ends a page, '/' the
 *                         last; "@1 ... @0" marks highlighted words, "%n"
 *                         puts picture n beside the page) and pictures
 *                         (8-bit TIMs, 104 x 100)
 *   GG/COM/RTIM.T 63-65   the "Income and expense report" (320 x 240): 63
 *                         or 64 after a success (chosen at random), 65
 *                         after a failure; each also holds the report's
 *                         digits "0123456789-" (88 x 16), white and red
 *
 * Writes:
 *   <out>/missions.toml          one [[mission]] per slot 0-19 (20-23 are
 *                                "Dummy" and left out), text pages as on the
 *                                disc with their markup
 *   <out>/MM-pic-K.png           mission MM's briefing picture K
 *   <out>/report-NN.png          RTIM NN's report picture (NN 63, 64, 65)
 *   <out>/report-digits.png      the white digits, report-digits-red.png the red
 * Returns 0 with a message in `err` on failure. */
#ifndef ACR_MISSIONS_MOA_H
#define ACR_MISSIONS_MOA_H

#include <stddef.h>
#include <stdint.h>

int acr_export_moa_missions(const char *unpacked_dir, const char *out_dir, uint32_t *count,
                            char *err, size_t err_size);

#endif
