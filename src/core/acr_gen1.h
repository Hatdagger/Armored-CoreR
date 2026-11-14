/* Gen 1 (Armored Core, Project Phantasma, Master of Arena) timing, measured
 * from Master of Arena (docs/research/gen1-timing.md).
 *
 * Every battle engine ends a frame by waiting until root counter 1, set to
 * count scanlines, shows at least 715 lines since the previous frame, then
 * shows the new frame. Logic runs one step per frame and never catches up,
 * so a full-speed step is 715 NTSC lines long:
 *
 *   715 lines x 3413 GPU clocks per line / 53,693,175 Hz = 45.449 ms (22.0 Hz)
 *
 * Every Gen 1 speed, drain and timer is counted in these steps. */
#ifndef ACR_GEN1_H
#define ACR_GEN1_H

#include <stdint.h>

#define ACR_GEN1_FRAME_LINES       715u        /* the engines' frame-end wait */
#define ACR_PS1_NTSC_GPU_HZ        53693175u   /* GPU video clock */
#define ACR_PS1_NTSC_CLOCKS_PER_LINE 3413u

/* 715 * 3413 / 53,693,175 s, in whole nanoseconds (45,448,886). */
#define ACR_GEN1_TICK_NS \
    ((uint64_t)ACR_GEN1_FRAME_LINES * ACR_PS1_NTSC_CLOCKS_PER_LINE * 1000000000ull / ACR_PS1_NTSC_GPU_HZ)

#endif
