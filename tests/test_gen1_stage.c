/* Arena stages: floors, steps, walls, ceilings and shots, on a small made-up
 * stage. With ACR_STAGE_DIR set to an import's stages folder it also checks
 * Green Silo (stage 27) from the user's own disc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acr_gen1_ai.h"
#include "acr_gen1_battle.h"
#include "acr_gen1_stage.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const acr_gen1_root_key k_walk[] = { { 2, 0, 0, -248 }, { 4, 0, 0, -548 } };
static const acr_gen1_root_key k_jump[] = { { 7, 0, 0, 0 }, { 9, 0, -400, 0 } };

static acr_gen1_ac_def make_def(const acr_gen1_stage *stage) {
    acr_gen1_ac_def d;
    memset(&d, 0, sizeof d);
    d.stats.energy_capacity = 28000;
    d.stats.rise_drain = 400;
    d.stats.refill_standing = 200;
    d.stats.refill_moving = 100;
    d.stats.red_zone = 8000;
    d.stats.thrust = 32; d.stats.thrust_80 = 25; d.stats.thrust_120 = 38;
    d.stats.turn_accel = 22; d.stats.turn_speed = 41; d.stats.friction = 43;
    acr_gen1_motion_from_stats(&d.stats, &d.motion);
    static acr_gen1_root_track walk = { 4, 2, k_walk }, jump = { 9, 2, k_jump };
    d.anim[ACR_GEN1_ANIM_WALK] = &walk;
    d.anim[ACR_GEN1_ANIM_JUMP] = &jump;
    d.landing_steps = 20;
    d.stage = stage;
    return d;
}

static void quad(acr_gen1_stage *s, int32_t a0, int32_t a1, int32_t a2, int32_t b0, int32_t b1, int32_t b2,
                 int32_t c0, int32_t c1, int32_t c2, int32_t d0, int32_t d1, int32_t d2) {
    const int32_t v[4][3] = { { a0, a1, a2 }, { b0, b1, b2 }, { c0, c1, c2 }, { d0, d1, d2 } };
    EXPECT(acr_gen1_stage_add(s, v, 4));
}

static int64_t sqrt_i(int64_t v) {
    int64_t r = 0;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
}

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *t = malloc((size_t)n + 1);
    if (t && fread(t, 1, (size_t)n, f) != (size_t)n) { free(t); t = NULL; }
    if (t) t[n] = 0;
    fclose(f);
    return t;
}

int main(void) {
    /* The made-up stage (y down): a floor at 0 from -5000 to 5000; a step
     * 100 high from x 1000 to 2000; a wall at x = 3000; a ceiling 1,500 up
     * over x < -2000. */
    acr_gen1_stage st;
    memset(&st, 0, sizeof st);
    quad(&st, -5000, 0, -5000, 5000, 0, -5000, 5000, 0, 5000, -5000, 0, 5000);
    quad(&st, 1000, -100, -5000, 2000, -100, -5000, 2000, -100, 5000, 1000, -100, 5000);
    quad(&st, 1000, 0, -5000, 1000, -100, -5000, 1000, -100, 5000, 1000, 0, 5000);   /* step's face */
    quad(&st, 3000, 0, -5000, 3000, -3000, -5000, 3000, -3000, 5000, 3000, 0, 5000);
    quad(&st, -5000, -1500, -5000, -2000, -1500, -5000, -2000, -1500, 5000, -5000, -1500, 5000);
    EXPECT(st.polys == 5);
    EXPECT(st.poly[0].flat && st.poly[1].flat && !st.poly[2].flat && !st.poly[3].flat && st.poly[4].flat);

    int32_t y = 1;
    EXPECT(acr_gen1_stage_floor(&st, 0, 0, -120, &y) && y == 0);
    EXPECT(acr_gen1_stage_floor(&st, 1500, 0, -120, &y) && y == -100);   /* on the step */
    EXPECT(acr_gen1_stage_floor(&st, 1500, 0, -50, &y) && y == 0);       /* from under it */
    EXPECT(!acr_gen1_stage_floor(&st, 9000, 0, -120, &y));               /* off the stage */
    EXPECT(acr_gen1_stage_ceiling(&st, -3000, 0, -300, &y) && y == -1500);
    EXPECT(!acr_gen1_stage_ceiling(&st, 0, 0, -300, &y));

    /* A body against the wall is pushed back to 75 short of it. */
    int32_t x = 2980, z = 0;
    EXPECT(acr_gen1_stage_push(&st, &x, &z, 0, 212, 75, 120));
    EXPECT(x >= 2920 && x <= 2926 && z == 0);
    x = 2000; z = 0;
    EXPECT(!acr_gen1_stage_push(&st, &x, &z, 0, 212, 75, 120) && x == 2000);

    /* Shots: stopped by the wall at x = 3000, a quarter of the way to 7000
     * from 2000 (out of 1024ths). */
    {
        const int32_t a[3] = { 2000, -500, 0 }, b[3] = { 6000, -500, 0 };
        int32_t t = -1;
        EXPECT(acr_gen1_stage_segment(&st, a, b, &t) && t >= 254 && t <= 258);
        const int32_t c[3] = { -1000, -500, 0 }, d[3] = { 2500, -500, 0 };
        EXPECT(!acr_gen1_stage_segment(&st, c, d, &t));
    }

    /* An AC walking toward +x: up the step, then stopped by the wall. */
    {
        acr_gen1_ac_def def = make_def(&st);
        acr_gen1_ac ac;
        acr_gen1_ac_spawn(&ac, &def, 0, 0, 1024);      /* facing +x */
        const acr_gen1_input fwd = { 1, 0, 0, 0, 0, 0, 0 };
        int on_step = 0;
        for (int t = 0; t < 400; ++t) {
            acr_gen1_ac_step(&ac, &def, fwd);
            if (ac.x / 64 > 1100 && ac.x / 64 < 1900 && ac.y == -100 * 64) on_step = 1;
        }
        EXPECT(on_step);
        EXPECT(ac.x / 64 >= 2900 && ac.x / 64 <= 2926);
        EXPECT(ac.state == ACR_GEN1_GROUND && ac.y == 0);
        /* Turn round and walk back off the step: down again to the floor. */
        acr_gen1_ac_set_yaw(&ac, 3072);
        for (int t = 0; t < 200; ++t) acr_gen1_ac_step(&ac, &def, fwd);
        EXPECT(ac.x / 64 < 1000 && ac.y == 0);
    }

    /* Rising under the ceiling stops with the head at it; the fall lands on the floor. */
    {
        acr_gen1_ac_def def = make_def(&st);
        acr_gen1_ac ac;
        acr_gen1_ac_spawn(&ac, &def, -3000 * 64, 0, 0);
        const acr_gen1_input up = { 0, 0, 0, 1, 0, 0, 0 }, none = { 0, 0, 0, 0, 0, 0, 0 };
        int32_t top = 0;
        for (int t = 0; t < 120; ++t) {
            acr_gen1_ac_step(&ac, &def, up);
            if (ac.y < top) top = ac.y;
        }
        EXPECT(top / 64 >= -1500 + 212 - 1 && top / 64 <= -1500 + 212 + 1);
        for (int t = 0; t < 200; ++t) acr_gen1_ac_step(&ac, &def, none);
        EXPECT(ac.y == 0 && ac.state != ACR_GEN1_AIR);
    }

    /* Off the edge of the stage the AC falls (no floor). */
    {
        acr_gen1_ac_def def = make_def(&st);
        acr_gen1_ac ac;
        acr_gen1_ac_spawn(&ac, &def, -4900 * 64, 0, 3072);      /* facing -x */
        const acr_gen1_input fwd = { 1, 0, 0, 0, 0, 0, 0 };
        for (int t = 0; t < 200; ++t) acr_gen1_ac_step(&ac, &def, fwd);
        EXPECT(ac.state == ACR_GEN1_AIR && ac.y > 0);
    }

    /* A rifle shot at an AC behind the wall stops at the wall. */
    {
        static acr_gen1_battle b;
        memset(&b, 0, sizeof b);
        static acr_gen1_ac_def def;
        def = make_def(&st);
        b.stage = &st;
        b.step_ms = 45;
        for (int i = 0; i < 2; ++i) {
            acr_gen1_fighter *f = &b.f[i];
            f->def = &def;
            f->intercept_shot = -1;
            f->ap = f->ap_max = 8000;
            f->shell_factor = 27; f->energy_factor = 32;
        }
        acr_gen1_ac_spawn(&b.f[0].ac, &def, 2300 * 64, 0, 1024);   /* facing the wall */
        acr_gen1_ac_spawn(&b.f[1].ac, &def, 4500 * 64, 0, 3072);   /* behind it */
        acr_gen1_weapon *w = &b.f[0].weapon[0];
        memset(w, 0, sizeof *w);
        w->kind = ACR_GEN1_GUN; w->attack = 218; w->ammo = 200; w->range = 6100;
        w->interval = 5; w->speed = 700; w->shots = 1;
        b.f[0].weapons = 1; b.f[0].ammo[0] = 200;
        const acr_gen1_input none[2] = { { 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0 } };
        const acr_gen1_actions fire[2] = { { 1, 0, 0 }, { 0, 0, 0 } }, idle[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
        const uint16_t ammo = b.f[0].ammo[0];
        acr_gen1_battle_step(&b, none, fire);
        EXPECT(b.f[0].ammo[0] == ammo - 1);
        for (int t = 0; t < 10; ++t) acr_gen1_battle_step(&b, none, idle);
        int alive = 0;
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) alive += b.shot[k].active;
        EXPECT(alive == 0 && b.f[1].ap == 8000);
        /* Without the wall in the way it hits. */
        acr_gen1_ac_spawn(&b.f[1].ac, &def, 2800 * 64, 0, 3072);
        b.f[0].cooldown = 0; b.f[0].hold_left = 0;
        acr_gen1_battle_step(&b, none, fire);
        for (int t = 0; t < 10; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.f[1].ap < 8000);
    }

    /* The OBJ reader (y up in the file). */
    {
        acr_gen1_stage p;
        EXPECT(acr_gen1_stage_parse("v -10 5.5 -10\nv 10 5.5 -10\nv 10 5.5 10\nv -10 5.5 10\nf 1 2 3 4\n", &p));
        EXPECT(p.polys == 1 && p.poly[0].v[0][1] == -6 && p.poly[0].flat);
        acr_gen1_stage_free(&p);
    }

    /* Green Silo from the user's own import, when there is one. */
    const char *dir = getenv("ACR_STAGE_DIR");
    if (dir) {
        char path[1024];
        snprintf(path, sizeof path, "%s/27/collision.obj", dir);
        char *text = read_text(path);
        EXPECT(text != NULL);
        acr_gen1_stage silo;
        if (text && acr_gen1_stage_parse(text, &silo)) {
            printf("green silo: %u polygons, x %d..%d, y %d..%d\n", (unsigned)silo.polys, silo.lo[0], silo.hi[0],
                   silo.lo[1], silo.hi[1]);
            EXPECT(silo.polys == 396);
            /* Both starts stand on the floor (-662). */
            EXPECT(acr_gen1_stage_floor(&silo, -5000, 0, -800, &y) && y >= -663 && y <= -661);
            EXPECT(acr_gen1_stage_floor(&silo, 5000, 0, -800, &y) && y >= -663 && y <= -661);
            EXPECT(acr_gen1_stage_floor(&silo, 0, 0, -800, &y) && y >= -663 && y <= -661);
            /* The dome stops a walk outward before about 6,700. */
            acr_gen1_ac_def def = make_def(&silo);
            acr_gen1_ac ac;
            acr_gen1_ac_spawn(&ac, &def, -5000 * 64, 0, 3072);   /* facing -x, outward */
            ac.y = ac.py = ac.sy = -662 * 64;
            const acr_gen1_input fwd = { 1, 0, 0, 0, 0, 0, 0 };
            for (int t = 0; t < 300; ++t) acr_gen1_ac_step(&ac, &def, fwd);
            printf("green silo: walking out stops at x %d, y %d\n", ac.x / 64, ac.y / 64);
            EXPECT(ac.x / 64 > -6800 && ac.x / 64 < -6000 && ac.state != ACR_GEN1_AIR);
            /* The arch feet on the diagonals are solid (user, 2026-11-19): a
             * walk out along one stops at the foot, short of the dome wall. */
            {
                acr_gen1_ac_spawn(&ac, &def, 0, 0, 512);          /* toward +x +z */
                ac.y = ac.py = ac.sy = -662 * 64;
                for (int t = 0; t < 300; ++t) acr_gen1_ac_step(&ac, &def, fwd);
                const int64_t x = ac.x / 64, z = ac.z / 64;
                printf("green silo: walking along a diagonal stops at %d, %d (radius %d)\n", (int)x, (int)z,
                       (int)sqrt_i(x * x + z * z));
                EXPECT(sqrt_i(x * x + z * z) < 6300);   /* the foot's inner face is at about 6,350 */
            }
            /* Rising hits the roof (6,750 up). */
            const acr_gen1_input up = { 0, 0, 0, 1, 0, 0, 0 };
            acr_gen1_ac_spawn(&ac, &def, 0, 0, 0);
            ac.y = ac.py = ac.sy = -662 * 64;
            int32_t top = 0;
            for (int t = 0; t < 400; ++t) { acr_gen1_ac_step(&ac, &def, up); if (ac.y < top) top = ac.y; }
            printf("green silo: highest feet %d\n", top / 64);
            EXPECT(top / 64 > -6750 + 212 - 50);
            /* Two computer pilots fight two minutes in the dome: nobody gets
             * out through the wall or under the floor. */
            {
                static acr_gen1_battle b;
                memset(&b, 0, sizeof b);
                b.stage = &silo;
                b.step_ms = 45;
                const int32_t start[2][4] = { { -5000, -662, 0, 1024 }, { 5000, -662, 0, 3072 } };
                acr_gen1_ai ai[2];
                for (int i = 0; i < 2; ++i) {
                    acr_gen1_fighter *f = &b.f[i];
                    f->def = &def;
                    f->intercept_shot = -1; f->swing_target = -1; f->last_missile = -1;
                    f->ap = f->ap_max = 8000;
                    f->shell_factor = 27; f->energy_factor = 32;
                    acr_gen1_weapon *w = &f->weapon[0];
                    memset(w, 0, sizeof *w);
                    w->kind = ACR_GEN1_GUN; w->attack = 218; w->ammo = 2000; w->range = 6100;
                    w->lock_ms = 1; w->box_w = 240; w->box_h = 240; w->inner_w = 200; w->inner_h = 200;
                    w->lock_value = 60; w->interval = 5; w->speed = 700; w->shots = 1;
                    f->weapons = 1; f->ammo[0] = 2000;
                    acr_gen1_ac_spawn(&f->ac, &def, start[i][0] * 64, start[i][2] * 64, (uint16_t)start[i][3]);
                    f->ac.y = f->ac.py = f->ac.sy = start[i][1] * 64;
                    acr_gen1_ai_profile prof;
                    acr_gen1_ai_preset(&prof, i ? ACR_GEN1_AI_AGGRESSOR : ACR_GEN1_AI_BALANCED);
                    acr_gen1_ai_init(&ai[i], &prof, 77u + (uint32_t)i);
                }
                int32_t far = 0, low = -100000;
                for (int t = 0; t < 120 * 22 && b.f[0].ap > 0 && b.f[1].ap > 0; ++t) {
                    acr_gen1_input in[2];
                    acr_gen1_actions act[2];
                    for (int i = 0; i < 2; ++i) acr_gen1_ai_step(&ai[i], &b, i, 4700, &in[i], &act[i]);
                    acr_gen1_battle_step(&b, in, act);
                    for (int i = 0; i < 2; ++i) {
                        const int64_t x = b.f[i].ac.x / 64, z = b.f[i].ac.z / 64;
                        const int32_t r = (int32_t)sqrt_i(x * x + z * z);
                        if (r > far) far = r;
                        if (b.f[i].ac.y / 64 > low) low = b.f[i].ac.y / 64;
                    }
                }
                printf("green silo AI fight: farthest from the middle %d, lowest feet %d, AP %d / %d\n", far, low,
                       b.f[0].ap, b.f[1].ap);
                EXPECT(far < 6718 && low <= -661);
            }
            acr_gen1_stage_free(&silo);
        }
        free(text);
    }

    acr_gen1_stage_free(&st);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 stage: all passed\n");
    return 0;
}
