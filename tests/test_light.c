/* CPU lighting: GTE-style integer maths. */
#include <stdio.h>
#include "acr_light.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main(void) {
    acr_lighting l = { 64, 64, 64, 1, { { 0, -4096, 0, 128, 128, 128 } } };
    uint8_t c[3];
    acr_light_color(&l, 0, 4096, 0, 100, 100, 100, c);     /* facing the light: 64 + 128 = 1.5x */
    CHECK(c[0] == 150 && c[1] == 150 && c[2] == 150);
    acr_light_color(&l, 0, -4096, 0, 100, 100, 100, c);    /* facing away: ambient only, 0.5x */
    CHECK(c[0] == 50);
    acr_light_color(&l, 0, 4096, 0, 255, 0, 0, c);         /* saturates at 255 */
    CHECK(c[0] == 255 && c[1] == 0);
    acr_light_color(&l, 4096, 0, 0, 128, 128, 128, c);     /* side on: ambient only */
    CHECK(c[0] == 64);
    if (!fails) printf("light: all passed\n");
    return fails != 0;
}
