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

/* Two computer pilots (balanced and aggressor) fight two minutes on a
 * stage from their starts: the farthest either gets from the middle, the
 * lowest feet, the AP left. */
static void ai_fight(const acr_gen1_stage *st, const acr_gen1_ac_def *def, const int32_t start[2][4], int32_t *far,
                     int32_t *low, int32_t ap[2]) {
    static acr_gen1_battle b;
    memset(&b, 0, sizeof b);
    b.stage = st;
    b.step_ms = 45;
    acr_gen1_ai ai[2];
    for (int i = 0; i < 2; ++i) {
        acr_gen1_fighter *f = &b.f[i];
        f->def = def;
        f->intercept_shot = -1; f->swing_target = -1; f->last_missile = -1;
        f->ap = f->ap_max = 8000;
        f->shell_factor = 27; f->energy_factor = 32;
        acr_gen1_weapon *w = &f->weapon[0];
        memset(w, 0, sizeof *w);
        w->kind = ACR_GEN1_GUN; w->attack = 218; w->ammo = 2000; w->range = 6100;
        w->lock_ms = 1; w->box_w = 240; w->box_h = 240; w->inner_w = 200; w->inner_h = 200;
        w->lock_value = 60; w->interval = 5; w->speed = 700; w->shots = 1;
        f->weapons = 1; f->ammo[0] = 2000;
        acr_gen1_ac_spawn(&f->ac, def, start[i][0] * 64, start[i][2] * 64, (uint16_t)start[i][3]);
        f->ac.y = f->ac.py = f->ac.sy = start[i][1] * 64;
        acr_gen1_ai_profile prof;
        acr_gen1_ai_preset(&prof, i ? ACR_GEN1_AI_AGGRESSOR : ACR_GEN1_AI_BALANCED);
        acr_gen1_ai_init(&ai[i], &prof, 77u + (uint32_t)i);
    }
    *far = 0;
    *low = -100000;
    for (int t = 0; t < 120 * 22 && b.f[0].ap > 0 && b.f[1].ap > 0; ++t) {
        acr_gen1_input in[2];
        acr_gen1_actions act[2];
        for (int i = 0; i < 2; ++i) acr_gen1_ai_step(&ai[i], &b, i, 4700, &in[i], &act[i]);
        acr_gen1_battle_step(&b, in, act);
        for (int i = 0; i < 2; ++i) {
            const int64_t x = b.f[i].ac.x / 64, z = b.f[i].ac.z / 64;
            const int32_t r = (int32_t)sqrt_i(x * x + z * z);
            if (r > *far) *far = r;
            if (b.f[i].ac.y / 64 > *low) *low = b.f[i].ac.y / 64;
        }
    }
    ap[0] = b.f[0].ap;
    ap[1] = b.f[1].ap;
}

/* A polygon line for a made-up collision.txt (game axes, model units):
 * corners in strip order (outline 0-1-3-2), the flags worked out as the
 * game's (main axis, which corners are the box's min / max). */
static void gpoly(char *out, size_t n, const int32_t (*v)[3], const int idx[4], int nx, int ny, int nz, long d,
                  int inside, int cls) {
    const int ax = abs(nx) >= abs(ny) && abs(nx) >= abs(nz) ? 0 : abs(ny) >= abs(nz) ? 1 : 2;
    unsigned flags = 1u | (unsigned)ax << 2;
    for (int k = 0; k < 3; ++k) {
        int lo = 0, hi = 0;
        for (int j = 1; j < 4; ++j) {
            if (v[idx[j]][k] < v[idx[lo]][k]) lo = j;
            if (v[idx[j]][k] > v[idx[hi]][k]) hi = j;
        }
        flags |= (unsigned)lo << (4 + 2 * k) | (unsigned)hi << (10 + 2 * k);
    }
    snprintf(out, n, "p %u %d %d %d %ld %d %d %d %d\n", flags, nx, ny, nz, d, idx[0] * 8 + (inside ? 1 : 0),
             idx[1] * 8 + cls, idx[2] * 8, idx[3] * 8);
}

int main(void) {
    /* The made-up stage (y down): a floor at 0 from -5000 to 5000; a step
     * 60 high from x 1000 to 2000; a wall at x = 3000; a ceiling 1,500 up
     * over x < -2000. */
    acr_gen1_stage st;
    memset(&st, 0, sizeof st);
    quad(&st, -5000, 0, -5000, 5000, 0, -5000, 5000, 0, 5000, -5000, 0, 5000);
    quad(&st, 1000, -60, -5000, 2000, -60, -5000, 2000, -60, 5000, 1000, -60, 5000);
    quad(&st, 1000, 0, -5000, 1000, -60, -5000, 1000, -60, 5000, 1000, 0, 5000);   /* step's face */
    quad(&st, 3000, 0, -5000, 3000, -3000, -5000, 3000, -3000, 5000, 3000, 0, 5000);
    quad(&st, -5000, -1500, -5000, -2000, -1500, -5000, -2000, -1500, 5000, -5000, -1500, 5000);
    EXPECT(st.polys == 5);
    EXPECT(st.poly[0].flat && st.poly[1].flat && !st.poly[2].flat && !st.poly[3].flat && st.poly[4].flat);

    int32_t y = 1;
    EXPECT(acr_gen1_stage_floor(&st, 0, 0, -120, &y) && y == 0);
    EXPECT(acr_gen1_stage_floor(&st, 1500, 0, -120, &y) && y == -60);   /* on the step */
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
            if (ac.x / 64 > 1100 && ac.x / 64 < 1900 && ac.y == -60 * 64) on_step = 1;
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
        const acr_gen1_actions fire[2] = { { 1, 0, 0, 0 }, { 0, 0, 0, 0 } }, idle[2] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
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


    /* The game's own rules on a made-up stage (game axes: x is mirrored in
     * the engine). A floor at y 0 out to 2,000; a wall at game x 1,000
     * (engine x -1,000); a wall of a class ACs pass through at game x
     * -1,000; a lower floor at y 800 from z 2,000 to 7,500. */
    {
        static const int32_t v[16][3] = {
            { -8000, 0, -8000 }, { 8000, 0, -8000 }, { -8000, 0, 8000 }, { 8000, 0, 8000 },
            { 4000, -4000, -8000 }, { 4000, -4000, 8000 }, { 4000, 0, -8000 }, { 4000, 0, 8000 },
            { -4000, -4000, -8000 }, { -4000, -4000, 8000 }, { -4000, 0, -8000 }, { -4000, 0, 8000 },
            { -8000, 3200, 8000 }, { 8000, 3200, 8000 }, { -8000, 3200, 30000 }, { 8000, 3200, 30000 },
        };
        static char text[4096];
        int o = snprintf(text, sizeof text, "area -3000 -2000 -3000 3000 1000 11000 0 0 0 0 7\nmodel 7 16 4\n");
        for (int i = 0; i < 16; ++i) o += snprintf(text + o, sizeof text - (size_t)o, "v %d %d %d\n", v[i][0], v[i][1], v[i][2]);
        const int floor_i[4] = { 0, 1, 2, 3 }, wall_i[4] = { 4, 5, 6, 7 }, ghost_i[4] = { 8, 9, 10, 11 },
                  low_i[4] = { 12, 13, 14, 15 };
        gpoly(text + o, sizeof text - (size_t)o, v, floor_i, 0, 4096, 0, 0, 1, 0); o += (int)strlen(text + o);
        gpoly(text + o, sizeof text - (size_t)o, v, wall_i, 4096, 0, 0, -4000L * 4096, 1, 0); o += (int)strlen(text + o);
        gpoly(text + o, sizeof text - (size_t)o, v, ghost_i, -4096, 0, 0, -4000L * 4096, 1, 1); o += (int)strlen(text + o);
        gpoly(text + o, sizeof text - (size_t)o, v, low_i, 0, 4096, 0, -3200L * 4096, 1, 0);
        acr_gen1_stage g;
        memset(&g, 0, sizeof g);
        EXPECT(acr_gen1_stage_parse_game(text, &g));
        EXPECT(g.areas == 1 && g.gmodels == 1 && g.gmodel[0].polys == 4);
        /* One try: a step into the wall comes back short of it. */
        {
            const int32_t pos[3] = { -880, 0, 0 };
            int32_t step[3] = { -30, 0, 0 };
            EXPECT(acr_gen1_stage_try(&g, pos, step, ACR_GEN1_STAGE_BOX_HALF, ACR_GEN1_STAGE_BOX_HEIGHT,
                                      ACR_GEN1_STAGE_AC_MASK));
            printf("game rules: a step of -30 at x -880 becomes %d, %d, %d\n", step[0], step[1], step[2]);
            EXPECT(pos[0] + step[0] >= -900 && pos[0] + step[0] <= -897 && step[1] == 0 && step[2] == 0);
        }
        /* The floor probe finds the floor under the feet. */
        {
            const int32_t pos[3] = { 0, -30, 0 };
            int32_t f = 0;
            EXPECT(acr_gen1_stage_floor_probe(&g, pos, ACR_GEN1_STAGE_PROBE_HALF, ACR_GEN1_STAGE_BOX_HEIGHT,
                                              ACR_GEN1_STAGE_AC_MASK, &f) && f == 0);
        }
        acr_gen1_ac_def def = make_def(&g);
        const acr_gen1_input fwd = { 1, 0, 0, 0, 0, 0, 0 };
        acr_gen1_ac ac;
        /* Walking into the wall stops 100 short of it (the box's half width). */
        acr_gen1_ac_spawn(&ac, &def, 0, 0, 3072);
        for (int t = 0; t < 400; ++t) acr_gen1_ac_step(&ac, &def, fwd);
        printf("game rules: walking into the wall stops at x %d, y %d\n", ac.x / 64, ac.y / 64);
        /* Feet touching the floor's plane are pushed a unit up off it. */
        EXPECT(ac.x / 64 >= -900 && ac.x / 64 <= -896 && ac.y / 64 >= -1 && ac.y / 64 <= 0 &&
               ac.state == ACR_GEN1_GROUND);
        /* The other wall's class is one ACs pass through. */
        acr_gen1_ac_spawn(&ac, &def, 0, 0, 1024);
        int32_t far = 0;
        for (int t = 0; t < 400 && ac.state != ACR_GEN1_AIR; ++t) {
            acr_gen1_ac_step(&ac, &def, fwd);
            if (ac.x > far) far = ac.x;
        }
        printf("game rules: through the pass-through wall to x %d\n", far / 64);
        EXPECT(far / 64 > 1200);
        /* Off the edge (z 2,000): falls, lands on the lower floor at 800. */
        acr_gen1_ac_spawn(&ac, &def, 0, 1500 * 64, 0);
        int fell = 0;
        for (int t = 0; t < 600; ++t) {
            acr_gen1_ac_step(&ac, &def, fell ? (acr_gen1_input){ 0 } : fwd);
            if (ac.state == ACR_GEN1_AIR) fell = 1;
        }
        printf("game rules: walked off the edge (fell %d), now at z %d, y %d, state %d\n", fell, ac.z / 64, ac.y / 64,
               ac.state);
        EXPECT(fell && ac.state != ACR_GEN1_AIR && ac.y / 64 >= 797 && ac.y / 64 <= 800 && ac.z / 64 > 2000);
        /* Out of area, with the boundary shrunk to fit this little stage
         * (as a test; the made-up floor is open on every side): box 0 the
         * whole level, box 1 x / z within 600, box 2 x within 800, z 400. */
        {
            const acr_gen1_stage_bound bd[3] = {
                { { -3000, -5000, -3000 }, { 3000, 5000, 3000 }, 0 },
                { { -600, -5000, -600 }, { 600, 5000, 600 }, 1 },
                { { -800, -5000, -400 }, { 800, 5000, 400 }, 2 },
            };
            memcpy(g.bound, bd, sizeof bd);
            g.bounds = 3;
            EXPECT(acr_gen1_stage_has_area_rule(&g));
            EXPECT(acr_gen1_stage_area_bits(&g, 0, 0, 0) == 7);
            EXPECT(acr_gen1_stage_area_bits(&g, 700, 0, 0) == 5);
            EXPECT(acr_gen1_stage_area_bits(&g, 900, 0, 0) == 1);
            static acr_gen1_battle b;
            memset(&b, 0, sizeof b);
            b.stage = &g;
            b.step_ms = 45;
            for (int i = 0; i < 2; ++i) {
                b.f[i].def = &def;
                b.f[i].ap = b.f[i].ap_max = 8000;
                b.f[i].intercept_shot = -1; b.f[i].swing_target = -1; b.f[i].last_missile = -1;
            }
            acr_gen1_ac_spawn(&b.f[0].ac, &def, 0, 0, 3072);          /* walks toward engine -x */
            acr_gen1_ac_spawn(&b.f[1].ac, &def, 0, 300 * 64, 0);
            EXPECT(acr_gen1_battle_result(&b, 0) == 0 && acr_gen1_battle_out_of_area(&b, 0) == 0);
            int warned = 0, t = 0;
            for (; t < 400 && !acr_gen1_battle_result(&b, 0); ++t) {
                acr_gen1_input in[2] = { { 1, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0 } };
                acr_gen1_actions act[2];
                memset(act, 0, sizeof act);
                acr_gen1_battle_step(&b, in, act);
                if (acr_gen1_battle_out_of_area(&b, 0) == 1) warned = 1;
            }
            printf("game rules: out of area at x %d after %d ticks (warned %d), result %d\n", b.f[0].ac.x / 64, t,
                   warned, acr_gen1_battle_result(&b, 0));
            EXPECT(warned && acr_gen1_battle_result(&b, 0) == 2 && b.f[0].ac.x / 64 < -800);
            /* Both out: a draw. */
            b.f[1].ac.x = -2000 * 64;
            EXPECT(acr_gen1_battle_result(&b, 0) == 3);
            /* Outside box 0 as well (bits 0): not out of area, as in the
             * game's check (bits 0-2 exactly 1). */
            b.f[0].ac.x = -4000 * 64;
            EXPECT(acr_gen1_stage_area_bits(&g, -4000, b.f[0].ac.y / 64, 0) == 0);
            EXPECT(acr_gen1_battle_out_of_area(&b, 0) == 0);
            EXPECT(acr_gen1_battle_result(&b, 0) == 1);    /* only fighter 1 is out now: 0 wins */
        }
        acr_gen1_stage_free(&g);
    }

    /* Green Silo from the user's own import, when there is one. */
    const char *dir = getenv("ACR_STAGE_DIR");
    if (dir) {
        /* The mission maps (stages 0-19) load with the game's collision.
         * Known: 1 and 6's ground is class 1 / 4 polygons, which the arena's
         * body mask (5) passes through, so ACs fall through it there
         * (docs/research/gen1-stages.md, "Mission maps"). */
        for (int s = 0; s < 20; ++s) {
            char p1[1024], p2[1024];
            snprintf(p1, sizeof p1, "%s/%02d/collision.obj", dir, s);
            snprintf(p2, sizeof p2, "%s/%02d/collision.txt", dir, s);
            char *o = read_text(p1), *g = read_text(p2);
            acr_gen1_stage ms;
            memset(&ms, 0, sizeof ms);
            EXPECT(o && g && acr_gen1_stage_parse(o, &ms) && acr_gen1_stage_parse_game(g, &ms) && ms.areas > 0);
            free(o); free(g);
            acr_gen1_stage_free(&ms);
        }
        /* Mission map 11, the four-pillar hall (P4 Qualifier): the test
         * starts stand on the floor (3,000), and two computer pilots fight
         * two minutes there without falling or leaving the hall. */
        {
            char p1[1024], p2[1024];
            snprintf(p1, sizeof p1, "%s/11/collision.obj", dir);
            snprintf(p2, sizeof p2, "%s/11/collision.txt", dir);
            char *o = read_text(p1), *g = read_text(p2);
            acr_gen1_stage hall;
            memset(&hall, 0, sizeof hall);
            if (o && g && acr_gen1_stage_parse(o, &hall) && acr_gen1_stage_parse_game(g, &hall)) {
                EXPECT(acr_gen1_stage_floor(&hall, 0, -8000, 2000, &y) && y == 3000);
                EXPECT(acr_gen1_stage_floor(&hall, 0, 8000, 2000, &y) && y == 3000);
                const int32_t qs[2][4] = { { 0, 3000, -8000, 0 }, { 0, 3000, 8000, 2048 } };
                acr_gen1_ac_def hdef = make_def(&hall);
                int32_t far, low, ap[2];
                ai_fight(&hall, &hdef, qs, &far, &low, ap);
                printf("mission map 11: fight reached %d from the middle, lowest feet %d, AP %d / %d\n", far, low,
                       ap[0], ap[1]);
                EXPECT(low <= 3000 && far < 12000);
            } else {
                EXPECT(!"mission map 11 missing: re-import");
            }
            free(o); free(g);
            acr_gen1_stage_free(&hall);
        }
        char path[1024];
        snprintf(path, sizeof path, "%s/27/collision.obj", dir);
        char *text = read_text(path);
        EXPECT(text != NULL);
        acr_gen1_stage silo;
        if (text && acr_gen1_stage_parse(text, &silo)) {
            printf("green silo: %u polygons, x %d..%d, y %d..%d\n", (unsigned)silo.polys, silo.lo[0], silo.hi[0],
                   silo.lo[1], silo.hi[1]);
            /* The game's own collision: the rest of these checks then run
             * on the game's wall and floor rules. */
            snprintf(path, sizeof path, "%s/27/collision.txt", dir);
            char *game = read_text(path);
            EXPECT(game != NULL);
            EXPECT(game && acr_gen1_stage_parse_game(game, &silo));
            free(game);
            printf("green silo: game collision %u areas, %u models\n", (unsigned)silo.areas, (unsigned)silo.gmodels);
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
                const int32_t start[2][4] = { { -5000, -662, 0, 1024 }, { 5000, -662, 0, 3072 } };
                int32_t far, low, ap[2];
                ai_fight(&silo, &def, start, &far, &low, ap);
                printf("green silo AI fight: farthest from the middle %d, lowest feet %d, AP %d / %d\n", far, low, ap[0],
                       ap[1]);
                EXPECT(far < 6718 && low <= -661);
            }
            acr_gen1_stage_free(&silo);
        }
        free(text);

        /* Ocean Platform (stage 39, the user's pick for the next arena):
         * open to the sea, with polygons ACs pass through (class 1). The
         * starts stand on the platform; a fight runs. */
        snprintf(path, sizeof path, "%s/39/collision.obj", dir);
        text = read_text(path);
        snprintf(path, sizeof path, "%s/39/collision.txt", dir);
        char *game = read_text(path);
        acr_gen1_stage ocean;
        if (text && game && acr_gen1_stage_parse(text, &ocean)) {
            EXPECT(acr_gen1_stage_parse_game(game, &ocean));
            printf("ocean platform: %u polygons, %u areas, %u models\n", (unsigned)ocean.polys, (unsigned)ocean.areas,
                   (unsigned)ocean.gmodels);
            const int32_t start[2][4] = { { -38, -176, -9345, 0 }, { -6, -176, 9919, 2048 } };
            for (int i = 0; i < 2; ++i) {
                const int32_t pos[3] = { start[i][0], start[i][1], start[i][2] };
                int32_t f = 0;
                EXPECT(acr_gen1_stage_floor_probe(&ocean, pos, ACR_GEN1_STAGE_PROBE_HALF, ACR_GEN1_STAGE_BOX_HEIGHT,
                                                  ACR_GEN1_STAGE_AC_MASK, &f));
                printf("ocean platform: start %d floor %d\n", i + 1, f);
                EXPECT(f - pos[1] < ACR_GEN1_STAGE_GROUNDED);
            }
            acr_gen1_ac_def def = make_def(&ocean);
            int32_t far, low, ap[2];
            ai_fight(&ocean, &def, start, &far, &low, ap);
            printf("ocean platform AI fight: farthest from the middle %d, lowest feet %d, AP %d / %d\n", far, low,
                   ap[0], ap[1]);
            acr_gen1_stage_free(&ocean);
        }
        free(text);
        free(game);

        /* The stages' own out-of-area boxes (stage.toml out_of_area, from the
         * stage objects and the stage code): the starts are inside; a walk
         * straight out from start 1 ends out of area (or at a wall). Green
         * Silo has none. */
        for (int si = 0; si < 4; ++si) {
            static const int ids[4] = { 25, 27, 36, 39 };
            char p1[1024], p2[1024], p3[1024];
            snprintf(p1, sizeof p1, "%s/%02d/collision.obj", dir, ids[si]);
            snprintf(p2, sizeof p2, "%s/%02d/collision.txt", dir, ids[si]);
            snprintf(p3, sizeof p3, "%s/%02d/stage.toml", dir, ids[si]);
            char *o = read_text(p1), *g = read_text(p2), *t = read_text(p3);
            acr_gen1_stage sg;
            memset(&sg, 0, sizeof sg);
            EXPECT(o && g && t && acr_gen1_stage_parse(o, &sg) && acr_gen1_stage_parse_game(g, &sg));
            int32_t start[2][4] = { { 0 } };
            for (int k = 0; t && k < 2; ++k) {
                char key[16];
                snprintf(key, sizeof key, "start_%d = [", k + 1);
                const char *p = strstr(t, key);
                EXPECT(p && sscanf(p + strlen(key), "%d, %d, %d, %d", &start[k][0], &start[k][1], &start[k][2],
                                   &start[k][3]) == 4);
            }
            const char *p = t ? strstr(t, "\nout_of_area = [") : NULL;
            if (p) {
                p = strchr(p, '\n') + 1;
                p = strchr(p, '\n') + 1;
                int v[7];
                while (sg.bounds < ACR_GEN1_STAGE_BOUNDS &&
                       sscanf(p, " [%d, %d, %d, %d, %d, %d, %d]", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) == 7) {
                    acr_gen1_stage_bound *b = &sg.bound[sg.bounds++];
                    for (int k = 0; k < 3; ++k) { b->lo[k] = v[k]; b->hi[k] = v[3 + k]; }
                    b->bit = (uint8_t)v[6];
                    p = strchr(p, '\n') + 1;
                }
            }
            printf("stage %d: %u out-of-area boxes\n", ids[si], (unsigned)sg.bounds);
            EXPECT(ids[si] == 27 ? sg.bounds == 0 : sg.bounds == 3 && acr_gen1_stage_has_area_rule(&sg));
            if (sg.bounds) {
                acr_gen1_ac_def sdef = make_def(&sg);
                static acr_gen1_battle b;
                memset(&b, 0, sizeof b);
                b.stage = &sg;
                b.step_ms = 45;
                for (int i = 0; i < 2; ++i) {
                    b.f[i].def = &sdef;
                    b.f[i].ap = b.f[i].ap_max = 8000;
                    b.f[i].intercept_shot = -1; b.f[i].swing_target = -1; b.f[i].last_missile = -1;
                    acr_gen1_ac_spawn(&b.f[i].ac, &sdef, start[i][0] * 64, start[i][2] * 64, start[i][3]);
                    b.f[i].ac.y = b.f[i].ac.py = b.f[i].ac.sy = start[i][1] * 64;
                    EXPECT(acr_gen1_battle_out_of_area(&b, i) == 0);
                }
                /* Face away from the middle and walk out. */
                acr_gen1_ac_set_yaw(&b.f[0].ac, (uint16_t)((start[0][3] + 2048) & 4095));
                int warned = 0, tk = 0;
                for (; tk < 2000 && !acr_gen1_battle_result(&b, 0); ++tk) {
                    acr_gen1_input in[2] = { { 1, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0 } };
                    acr_gen1_actions act[2];
                    memset(act, 0, sizeof act);
                    acr_gen1_battle_step(&b, in, act);
                    if (acr_gen1_battle_out_of_area(&b, 0) == 1) warned = 1;
                }
                printf("stage %d: walking out from start 1 -> %d, %d, %d after %d ticks, warned %d, result %d\n",
                       ids[si], b.f[0].ac.x / 64, b.f[0].ac.y / 64, b.f[0].ac.z / 64, tk, warned,
                       acr_gen1_battle_result(&b, 0));
            }
            free(o); free(g); free(t);
            acr_gen1_stage_free(&sg);
        }
    }

    acr_gen1_stage_free(&st);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 stage: all passed\n");
    return 0;
}
