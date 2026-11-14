#include "acr_gen1_anim.h"

#include <stddef.h>

/* EXE 0x800135DC: step = total / frames, the remainder handed out with a
 * counter that starts at (frames + 1) / 2. C division truncates toward zero,
 * as the R3000's div does. */
void acr_gen1_stepper_init(acr_gen1_stepper *s, int frames, int total) {
    if (frames <= 0) frames = 1;
    const int q = total / frames;
    int r = total % frames;
    if (r < 0) r = -r;
    s->frames = (int16_t)frames;
    s->counter = (int16_t)((frames + 1) >> 1);
    s->dec = (int16_t)r;
    s->step = (int16_t)q;
    s->step_alt = (int16_t)(total < 0 ? q - 1 : q + 1);
}

/* VS engine 0x80054FF8: counter -= remainder; when it runs out, take the
 * longer step and refill the counter. */
int16_t acr_gen1_stepper_next(acr_gen1_stepper *s) {
    const int16_t c = (int16_t)(s->counter - s->dec);
    if (c > 0) {
        s->counter = c;
        return s->step;
    }
    s->counter = (int16_t)(c + s->frames);
    return s->step_alt;
}

int acr_gen1_root_span(const acr_gen1_root_track *t, unsigned frame,
                       int32_t delta[3], unsigned *frames) {
    uint16_t prev_frame = 0;
    int32_t px = 0, py = 0, pz = 0;
    for (uint16_t i = 0; i < t->count; ++i) {
        const acr_gen1_root_key *k = &t->keys[i];
        if (frame < k->frame) {
            delta[0] = k->x - px;
            delta[1] = k->y - py;
            delta[2] = k->z - pz;
            *frames = (unsigned)(k->frame - prev_frame);
            return 1;
        }
        prev_frame = k->frame;
        px = k->x; py = k->y; pz = k->z;
    }
    return 0;
}
