/* Rogue Arena's own sounds: the VS screen's count-in shots and shutter
 * slam, the victory sting and the type tick (AC1Recomp's, same recipe).
 * Built once into sfx's UI slots (sfx_set_ui); played with sfx_ui. */
#ifndef ROGUE_SOUND_H
#define ROGUE_SOUND_H

/* Makes them (after sfx_open). The number made; 0 = none (no audio, or
 * the import has no fight sounds for the VS shots). */
int rogue_sound_build(void);

#endif
