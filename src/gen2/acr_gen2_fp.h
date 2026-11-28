/* Single-precision arithmetic the PlayStation 2's way: its FPU cuts every
 * result off towards zero instead of rounding to nearest, so 20000 x 0.033
 * is 659.99994 there and 660 on a PC. Each operation is done exactly (in a
 * double) and then cut to a float, which gives the console's bits on every
 * machine and compiler, whatever its float settings. */
#ifndef ACR_GEN2_FP_H
#define ACR_GEN2_FP_H

#include <math.h>
#include <stdint.h>
#include <string.h>

static float g2_chop(double v)
{
    float f = (float)v;
    if (v >= 0.0 ? (double)f > v : (double)f < v) {
        uint32_t u;                 /* rounded away from zero: step back one */
        memcpy(&u, &f, sizeof u);
        u -= 1;
        memcpy(&f, &u, sizeof f);
    }
    return f;
}

static float g2_add(float a, float b) { return g2_chop((double)a + (double)b); }
static float g2_sub(float a, float b) { return g2_chop((double)a - (double)b); }
static float g2_mul(float a, float b) { return g2_chop((double)a * (double)b); }
static float g2_div(float a, float b) { return g2_chop((double)a / (double)b); }

#endif
