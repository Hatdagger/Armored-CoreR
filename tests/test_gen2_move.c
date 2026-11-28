/* Gen 2 compile and movement. The compile is checked against vectors that
 * tools/gen2/movement.py works out from the AC2 executable and parts
 * catalogue; the movement against what the rules in
 * docs/research/gen2-movement.md say should happen. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "acr_gen2_move.h"

#include "gen2_compile_vectors.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
#define NEAR(a, b, e) (fabsf((float)(a) - (float)(b)) <= (e))

/* Motion set 446's gait (core group 1, arms group 0, legs group 1): the set
 * of the first vector's parts. tools/gen2/movement.py --walk. */
static const acr_gen2_gait k_gait = {
    { 0.6f, 0.59962f, 0.59962f, 0.6f, 0.6f, 0.3f, 0.29935f, 0.29935f },
    { 1.5f, 1.414f, 1.414f, 1.5f, 1.5f, 1.0f, 1.131f, 1.131f },
    { 16, 16, 16, 16, 16, 16, 16, 16 },
    26, 18, 52
};

static const acr_gen2_tuning *T = &acr_gen2_tuning_ac2;

static acr_gen2_input in_none(void) { acr_gen2_input i; memset(&i, 0, sizeof i); return i; }
static acr_gen2_input in_fwd(void) { acr_gen2_input i = in_none(); i.forward = 1; return i; }
static acr_gen2_input in_boost(void) { acr_gen2_input i = in_none(); i.boost = 1; return i; }
static acr_gen2_input in_fwd_boost(void) { acr_gen2_input i = in_fwd(); i.boost = 1; return i; }

static void run(acr_gen2_ac *ac, const acr_gen2_stats *s, acr_gen2_input in, int frames)
{
    int i;
    for (i = 0; i < frames; i++) acr_gen2_move_step(ac, T, s, &k_gait, &in, 0.0f);
}

static float ground_speed(const acr_gen2_ac *ac) { return sqrtf(ac->vx * ac->vx + ac->vz * ac->vz); }

static void test_compile(void)
{
    size_t i, n = sizeof GEN2_VECTORS / sizeof GEN2_VECTORS[0];
    EXPECT(n == 7 + 31);     /* seven picked by hand, one per legs part */
    for (i = 0; i < n; i++) {
        acr_gen2_stats s;
        memset(&s, 0, sizeof s);
        acr_gen2_compile(T, &GEN2_VECTORS[i].b, &s);
        if (memcmp(&s, &GEN2_VECTORS[i].s, sizeof s) != 0) {
            printf("FAIL compile vector %u (%s)\n", (unsigned)i, GEN2_VECTORS[i].name);
            ++failures;
        }
    }
    EXPECT(acr_gen2_clampf(2.0f, 0.5f, 0.0f, 9.0f) == 1.5f);
    EXPECT(acr_gen2_clampf(0.1f, 1.0f, 0.3f, 2.0f) == 0.3f);
    EXPECT(acr_gen2_clampf(5.0f, 1.0f, 0.3f, 2.0f) == 2.0f);
}

/* Another Age: slower in the air, dearer to boost, slower to refill; a strong
 * booster counts for more. The ground values do not change. */
static void test_another_age(void)
{
    acr_gen2_stats a, b;
    acr_gen2_compile(&acr_gen2_tuning_ac2, &GEN2_VECTORS[0].b, &a);
    acr_gen2_compile(&acr_gen2_tuning_aa, &GEN2_VECTORS[0].b, &b);
    EXPECT(b.flymv_maxspd < a.flymv_maxspd);
    EXPECT(b.boost_sub > a.boost_sub);
    EXPECT(b.boost_add < a.boost_add);
    EXPECT(b.turn_maxspd == a.turn_maxspd);
    EXPECT(b.jumpspd == a.jumpspd);
    acr_gen2_compile(&acr_gen2_tuning_ac2, &GEN2_VECTORS[2].b, &a);   /* 21000-power booster */
    acr_gen2_compile(&acr_gen2_tuning_aa, &GEN2_VECTORS[2].b, &b);
    EXPECT(a.boost_factor == 1.15f);
    EXPECT(b.boost_factor > 1.39f && b.boost_factor < 1.41f);
}

static void test_directions(void)
{
    acr_gen2_input i = in_none();
    EXPECT(acr_gen2_dir(&i) == ACR_GEN2_DIR_NONE);
    i.forward = 1; EXPECT(acr_gen2_dir(&i) == ACR_GEN2_DIR_FWD);
    i.left = 1;    EXPECT(acr_gen2_dir(&i) == ACR_GEN2_DIR_FWD_L);
    i.forward = 0; EXPECT(acr_gen2_dir(&i) == ACR_GEN2_DIR_L);
    i.back = 1;    EXPECT(acr_gen2_dir(&i) == ACR_GEN2_DIR_BACK_L);
    i.left = 0; i.right = 1; EXPECT(acr_gen2_dir(&i) == ACR_GEN2_DIR_BACK_R);
}

/* Two legs walk at the clip's speed, straight ahead (-z at yaw 0). */
static void test_walk(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    acr_gen2_move_init(&ac, s);
    run(&ac, s, in_fwd(), 1);
    EXPECT(ac.state == ACR_GEN2_ST_WALK_START);
    run(&ac, s, in_fwd(), 30);
    EXPECT(ac.state == ACR_GEN2_ST_WALK);
    run(&ac, s, in_fwd(), 120);
    EXPECT(NEAR(ground_speed(&ac), 0.6f, 1e-5f));
    EXPECT(ac.vz < 0.0f && NEAR(ac.vx, 0.0f, 1e-5f));
    EXPECT(NEAR(ac.speed_shown * ACR_GEN2_KMH_PER_SPEED, 129.6f, 0.1f));    /* what the HUD would read */
    EXPECT(ac.energy == s->boost_max);
    run(&ac, s, in_none(), 20);
    EXPECT(ac.state == ACR_GEN2_ST_STAND && ground_speed(&ac) == 0.0f);
}

static void test_turn(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    acr_gen2_input in = in_none();
    acr_gen2_move_init(&ac, s);
    in.turn = 1;
    run(&ac, s, in, 3);
    EXPECT(ac.state == ACR_GEN2_ST_TURN);
    run(&ac, s, in, 12);
    EXPECT(NEAR(ac.yaw_speed, s->turn_maxspd, 1e-7f));     /* 10 frames to top turn speed */
    /* Two legs: the booster does nothing while turning on the spot. */
    in.boost = 1;
    run(&ac, s, in, 5);
    EXPECT(ac.state == ACR_GEN2_ST_TURN);
    in.turn = 0;
    run(&ac, s, in, 3);
    EXPECT(ac.state == ACR_GEN2_ST_CROUCH);
}

/* Ground boost: to top speed at half the air cost, a coast after letting go,
 * then a slide the pilot cannot cut short. */
static void test_ground_boost(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    int e0, frames = 0;
    acr_gen2_move_init(&ac, s);
    run(&ac, s, in_fwd_boost(), 1);
    EXPECT(ac.state == ACR_GEN2_ST_DASH);
    run(&ac, s, in_fwd_boost(), 60);
    EXPECT(NEAR(ground_speed(&ac), s->dash_maxspd, 1e-5f));
    e0 = ac.energy;
    run(&ac, s, in_fwd_boost(), 10);
    EXPECT(e0 - ac.energy == 10 * (s->boost_sub / 2));
    /* Let go: 14 more frames of boost, then the slide. */
    run(&ac, s, in_fwd(), 14);
    EXPECT(ac.state == ACR_GEN2_ST_DASH);
    run(&ac, s, in_fwd(), 1);
    EXPECT(ac.state == ACR_GEN2_ST_BRAKE);
    while (ac.state == ACR_GEN2_ST_BRAKE && frames < 200) {
        run(&ac, s, in_boost(), 1);         /* mashing does nothing until it is over */
        frames++;
    }
    EXPECT(ac.state == ACR_GEN2_ST_CROUCH);  /* ... and then the held booster jumps at once */
    EXPECT(frames >= 21 + 17 && frames < 60);
}

/* Boost, let go, boost again inside the coast: a hop into the air. */
static void test_dash_jump(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    acr_gen2_move_init(&ac, s);
    run(&ac, s, in_fwd_boost(), 30);
    run(&ac, s, in_fwd(), 3);
    run(&ac, s, in_fwd_boost(), 1);
    EXPECT(ac.state == ACR_GEN2_ST_AIR);
    EXPECT(ac.y < 0.0f);
}

/* The booster alone: squat for the clip's length, then leave the ground at the
 * legs' jump speed. With the button let go gravity takes it back down. */
static void test_jump(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    float top = 0.0f;
    int frames = 0;
    acr_gen2_move_init(&ac, s);
    run(&ac, s, in_boost(), 1);
    EXPECT(ac.state == ACR_GEN2_ST_CROUCH);
    run(&ac, s, in_none(), k_gait.crouch_frames);
    EXPECT(ac.state == ACR_GEN2_ST_CROUCH && ac.y == 0.0f);
    run(&ac, s, in_none(), 1);
    EXPECT(ac.state == ACR_GEN2_ST_AIR);
    EXPECT(NEAR(ac.y, -s->jumpspd, 1e-6f));    /* the first frame up has no gravity */
    while (ac.state == ACR_GEN2_ST_AIR && frames < 400) {
        run(&ac, s, in_none(), 1);
        if (-ac.y > top) top = -ac.y;
        frames++;
    }
    /* v^2 / 2g = 1.334^2 / 0.084 = 21.2 units, plus the gravity-free first frame. */
    EXPECT(top > 21.2f && top < 22.6f);
    EXPECT(ac.state == ACR_GEN2_ST_LAND);       /* 1.33 a frame is under the hard-landing 1.48 */
    EXPECT(ac.y == 0.0f);
    run(&ac, s, in_none(), k_gait.land_frames + 2);
    EXPECT(ac.state == ACR_GEN2_ST_STAND);
}

/* Flying: the booster lifts to the rise speed and drains every frame; run dry
 * and it cuts out until the bar is full again. */
static void test_flight_and_charge(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    int e0, frames = 0;
    acr_gen2_move_init(&ac, s);
    run(&ac, s, in_boost(), k_gait.crouch_frames + 2);
    EXPECT(ac.state == ACR_GEN2_ST_AIR);
    run(&ac, s, in_boost(), 100);
    EXPECT(NEAR(ac.vy, -s->flyup_maxspd, 1e-6f));
    e0 = ac.energy;
    run(&ac, s, in_fwd_boost(), 10);
    EXPECT(e0 - ac.energy == 10 * s->boost_sub);
    run(&ac, s, in_fwd_boost(), 60);
    EXPECT(NEAR(ground_speed(&ac), s->flymv_maxspd, 1e-5f));
    while (!ac.charging && frames < 5000) {
        run(&ac, s, in_boost(), 1);
        frames++;
    }
    EXPECT(ac.charging && ac.energy == 0);
    run(&ac, s, in_boost(), 30);
    EXPECT(ac.vy > 0.0f);                        /* falling with the button held */
    frames = 0;
    while (ac.state != ACR_GEN2_ST_STAND && frames < 2000) {
        run(&ac, s, in_none(), 1);
        frames++;
    }
    EXPECT(ac.state == ACR_GEN2_ST_STAND);
    /* Charging refills at half the standing rate, and locks the booster. */
    e0 = ac.energy;
    run(&ac, s, in_fwd_boost(), 10);
    EXPECT(ac.charging && ac.state != ACR_GEN2_ST_DASH);
    frames = 0;
    while (ac.charging && frames < 20000) {
        run(&ac, s, in_none(), 1);
        frames++;
    }
    EXPECT(!ac.charging && ac.energy == s->boost_max);
    EXPECT(frames > s->boost_max / s->boost_add);
}

/* The red zone: boosting costs a quarter (and refills a quarter). */
static void test_red_zone(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    int e0;
    acr_gen2_move_init(&ac, s);
    run(&ac, s, in_boost(), k_gait.crouch_frames + 2);
    ac.energy = s->boost_redarea - 1;
    e0 = ac.energy;
    run(&ac, s, in_boost(), 4);
    EXPECT(e0 - ac.energy == 4 * (s->boost_sub / 4));
}

/* A long fall ends at the top fall speed and in the hard landing. */
static void test_hard_landing(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    acr_gen2_input in = in_fwd();
    int frames = 0;
    acr_gen2_move_init(&ac, s);
    ac.y = -200.0f;
    run(&ac, s, in, 1);
    EXPECT(ac.state == ACR_GEN2_ST_AIR);
    while (ac.state == ACR_GEN2_ST_AIR && frames < 1000) {
        run(&ac, s, in, 1);
        frames++;
    }
    EXPECT(ac.state == ACR_GEN2_ST_HARD_LAND);
    run(&ac, s, in_fwd_boost(), 20);
    EXPECT(ac.state == ACR_GEN2_ST_HARD_LAND);           /* no way out */
    run(&ac, s, in_none(), 40);
    EXPECT(ac.state == ACR_GEN2_ST_STAND);
}

static void test_overboost(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    acr_gen2_input in = in_none();
    int e0;
    acr_gen2_move_init(&ac, s);
    in.overboost = 1;
    run(&ac, s, in, 1);
    in.overboost = 0;
    run(&ac, s, in, ACR_GEN2_OB_CHARGE_FRAMES - 2);
    EXPECT(ac.state == ACR_GEN2_ST_STAND);
    e0 = ac.energy;
    run(&ac, s, in, 1);
    EXPECT(ac.state == ACR_GEN2_ST_OVERBOOST);
    EXPECT(e0 - ac.energy >= s->oboost_1stsub);
    run(&ac, s, in, 1);
    EXPECT(NEAR(ground_speed(&ac), s->oboost_spd0, 1e-5f));      /* at full speed on the first frame */
    e0 = ac.energy;
    run(&ac, s, in, 10);
    EXPECT(e0 - ac.energy == 10 * s->oboost_sub);
    EXPECT(ac.vz < 0.0f);
    /* It burns until the bar is empty, then the AC slides and charges. */
    run(&ac, s, in, 2000);
    EXPECT(ac.state == ACR_GEN2_ST_STAND);
    /* Out of an overboost the speed bleeds off at the air acceleration. */
    acr_gen2_move_init(&ac, s);
    ac.vz = -3.0f;
    ac.y = -50.0f;
    ac.state = ACR_GEN2_ST_AIR;
    run(&ac, s, in_fwd_boost(), 2);
    EXPECT(NEAR(ground_speed(&ac), 3.0f - 2.0f * s->flymv_accel, 1e-4f));
}

/* Caterpillars glide at their moving speed and the booster lifts them straight off. */
static void test_caterpillar(void)
{
    acr_gen2_stats s;
    acr_gen2_ac ac;
    acr_gen2_compile(T, &GEN2_VECTORS[4].b, &s);
    EXPECT(s.leg_kind == ACR_GEN2_LEGS_CATERPILLAR);
    acr_gen2_move_init(&ac, &s);
    run(&ac, &s, in_fwd(), 80);
    EXPECT(ac.state == ACR_GEN2_ST_WALK);
    EXPECT(NEAR(ground_speed(&ac), s.dash_maxspd, 1e-5f));
    run(&ac, &s, in_boost(), 2);
    EXPECT(ac.state == ACR_GEN2_ST_AIR);
    run(&ac, &s, in_none(), 20);
    EXPECT(ac.state == ACR_GEN2_ST_LAND || ac.state == ACR_GEN2_ST_STAND);
}

/* Hovers float at their moving speed, and boost along the ground on their own booster. */
static void test_hover(void)
{
    acr_gen2_stats s;
    acr_gen2_ac ac;
    acr_gen2_compile(T, &GEN2_VECTORS[5].b, &s);
    EXPECT(s.leg_kind == ACR_GEN2_LEGS_HOVER);
    acr_gen2_move_init(&ac, &s);
    run(&ac, &s, in_fwd(), 80);
    EXPECT(NEAR(ground_speed(&ac), s.flymv_maxspd, 1e-5f));
    run(&ac, &s, in_fwd_boost(), 60);
    EXPECT(ac.state == ACR_GEN2_ST_DASH);
    EXPECT(NEAR(ground_speed(&ac), s.dash_maxspd, 1e-5f));
    EXPECT(s.dash_maxspd > s.flymv_maxspd);
}

/* Same inputs, same result, bit for bit. */
static unsigned script_hash(const acr_gen2_stats *s)
{
    acr_gen2_ac ac;
    unsigned h = 2166136261u, k;
    int f;
    acr_gen2_move_init(&ac, s);
    for (f = 0; f < 3000; f++) {
        acr_gen2_input in = in_none();
        int p = f / 50;
        in.forward = (p % 3) != 2;
        in.left = (p % 5) == 1;
        in.right = (p % 7) == 3;
        in.back = (p % 11) == 5;
        in.turn = (int8_t)((p % 4) == 1 ? 1 : (p % 4) == 3 ? -1 : 0);
        in.boost = (p % 2) == 0 || (f % 50) > 40;
        in.overboost = f == 1500;
        acr_gen2_move_step(&ac, T, s, &k_gait, &in, 0.0f);
        memcpy(&k, &ac.x, 4); h = (h ^ k) * 16777619u;
        memcpy(&k, &ac.y, 4); h = (h ^ k) * 16777619u;
        memcpy(&k, &ac.z, 4); h = (h ^ k) * 16777619u;
        memcpy(&k, &ac.yaw, 4); h = (h ^ k) * 16777619u;
        h = (h ^ (unsigned)ac.energy) * 16777619u;
        h = (h ^ ac.state) * 16777619u;
        EXPECT(ac.x == ac.x && ac.y == ac.y && ac.z == ac.z);      /* never NaN */
        EXPECT(ac.y <= 0.0f);
        EXPECT(ac.energy >= 0 && ac.energy <= s->boost_max);
    }
    return h;
}

int main(void)
{
    acr_gen2_stats s;
    test_compile();
    test_another_age();
    test_directions();
    acr_gen2_compile(T, &GEN2_VECTORS[0].b, &s);
    test_walk(&s);
    test_turn(&s);
    test_ground_boost(&s);
    test_dash_jump(&s);
    test_jump(&s);
    test_flight_and_charge(&s);
    test_red_zone(&s);
    test_hard_landing(&s);
    test_overboost(&s);
    test_caterpillar();
    test_hover();
    EXPECT(script_hash(&s) == script_hash(&s));
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("gen2 move: ok\n");
    return 0;
}
