/* Gen 1 root motion stepper and track spans (synthetic tracks). */
#include <stdio.h>

#include "acr_gen1_anim.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main(void) {
    /* The stepper hands out the whole total, never more than one apart. */
    for (int frames = 1; frames <= 9; ++frames)
        for (int total = -300; total <= 300; total += 7) {
            acr_gen1_stepper s;
            acr_gen1_stepper_init(&s, frames, total);
            int sum = 0, lo = 1 << 30, hi = -(1 << 30);
            for (int i = 0; i < frames; ++i) {
                const int v = acr_gen1_stepper_next(&s);
                sum += v;
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            EXPECT(sum == total);
            EXPECT(hi - lo <= 1);
        }
    /* 5 over 2: counter 1, remainder 1 -> the longer step first. */
    acr_gen1_stepper s;
    acr_gen1_stepper_init(&s, 2, 5);
    EXPECT(acr_gen1_stepper_next(&s) == 3);
    EXPECT(acr_gen1_stepper_next(&s) == 2);

    static const acr_gen1_root_key keys[] = { { 2, 0, 0, -100 }, { 5, 0, -30, -250 } };
    const acr_gen1_root_track t = { 5, 2, keys };
    int32_t d[3];
    unsigned n;
    EXPECT(acr_gen1_root_span(&t, 0, d, &n) && n == 2 && d[2] == -100);
    EXPECT(acr_gen1_root_span(&t, 3, d, &n) && n == 3 && d[1] == -30 && d[2] == -150);
    EXPECT(!acr_gen1_root_span(&t, 5, d, &n));
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 anim: all passed\n");
    return 0;
}
