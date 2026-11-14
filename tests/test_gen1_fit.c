/* Gen 1 fitting rules. Synthetic part numbers only (no game data in the
 * repo); the flags are the Master of Arena index rules. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_fit.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_part mk(uint16_t weight, uint16_t drain) {
    acr_gen1_part p = {0};
    p.present = 1; p.weight = weight; p.energy_drain = drain;
    return p;
}

/* A legal build: humanoid legs 3, regular arms 1, missile + radar on the back. */
static acr_gen1_fit_build base(void) {
    acr_gen1_fit_build b;
    memset(&b, 0, sizeof b);
    b.parts.core = mk(1000, 300);   b.parts.core.carry_limit = 2000;
    b.parts.booster = mk(200, 200);
    b.parts.head = mk(200, 200);
    b.parts.arms = mk(800, 300);
    b.parts.blade = mk(100, 100);
    b.parts.hand = mk(400, 10);
    b.parts.back_l = mk(250, 250);
    b.parts.back_r = mk(200, 250);
    b.parts.generator = mk(500, 4000);
    b.parts.fcs = mk(100, 100);
    b.parts.legs = mk(1800, 1500);  b.parts.legs.carry_limit = 4000;
    b.flags[ACR_GEN1_SLOT_ARMS] = acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_ARMS, 1);
    b.flags[ACR_GEN1_SLOT_LEGS] = acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_LEGS, 3);
    b.flags[ACR_GEN1_SLOT_HAND] = acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_HAND, 1);
    b.flags[ACR_GEN1_SLOT_BACK_L] = acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_BACK_L, 0);
    b.flags[ACR_GEN1_SLOT_BACK_R] = acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_BACK_R, 40);
    b.core_option_slots = 3;
    for (int i = 0; i < 16; ++i) b.option_cost[i] = 1;
    return b;
}

static void test_flags(void) {
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_ARMS, 11) == 0);
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_ARMS, 12) & ACR_GEN1_FIT_ARM_WEAPON);
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_ARMS, 5) == ACR_GEN1_FIT_NO_BACK_WEAPONS);
    EXPECT(!(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_LEGS, 26) & ACR_GEN1_FIT_CATERPILLAR));
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_LEGS, 27) & ACR_GEN1_FIT_CATERPILLAR);
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_LEGS, 6) == 0);
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_LEGS, 5) == ACR_GEN1_FIT_NO_HEAVY_HAND);
    EXPECT(!(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_LEGS, 29) & ACR_GEN1_FIT_NO_HEAVY_HAND));
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_BACK_L, 43) == 0);
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_BACK_R, 46) == ACR_GEN1_FIT_RADAR);
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_BACK_L, 52) == ACR_GEN1_FIT_TWO_SLOT);
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_BACK_L, 53) == (ACR_GEN1_FIT_TWO_SLOT | ACR_GEN1_FIT_BACK_WEAPON));
    EXPECT(acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_BACK_L, 39) == ACR_GEN1_FIT_BACK_WEAPON);
}

static void test_check(void) {
    acr_gen1_fit_build b = base();
    acr_gen1_fit_numbers n;
    EXPECT(acr_gen1_fit_check(&b, &n) == 0);
    EXPECT(n.energy_surplus == 4000 - (300 + 200 + 200 + 300 + 100 + 10 + 250 + 250 + 100 + 1500));
    EXPECT(n.arm_load == 800 + 100 + 400);
    EXPECT(n.carried_weight == 1000 + 200 + 200 + 800 + 100 + 400 + 250 + 200 + 500 + 100);

    b = base(); b.parts.legs.energy_drain = 2291;                    /* surplus -1 */
    EXPECT(acr_gen1_fit_check(&b, &n) == ACR_GEN1_FAIL_ENERGY && n.energy_surplus == -1);
    b = base(); b.parts.legs.energy_drain = 2290;
    EXPECT(acr_gen1_fit_check(&b, NULL) == 0);

    b = base(); b.parts.core.carry_limit = 1299;
    EXPECT(acr_gen1_fit_check(&b, NULL) == ACR_GEN1_FAIL_ARM_LOAD);
    b.flags[ACR_GEN1_SLOT_ARMS] = ACR_GEN1_FIT_ARM_WEAPON;           /* only the arms count */
    EXPECT(acr_gen1_fit_check(&b, &n) == 0 && n.arm_load == 800);

    b = base(); b.parts.legs.carry_limit = 3749;
    EXPECT(acr_gen1_fit_check(&b, NULL) == ACR_GEN1_FAIL_WEIGHT);

    b = base(); b.parts.booster.present = 0;
    EXPECT(acr_gen1_fit_check(&b, NULL) & ACR_GEN1_FAIL_NO_BOOSTER);
    b.flags[ACR_GEN1_SLOT_LEGS] = ACR_GEN1_FIT_CATERPILLAR;
    EXPECT(!(acr_gen1_fit_check(&b, NULL) & ACR_GEN1_FAIL_NO_BOOSTER));

    b = base(); b.parts.fcs.present = 0;
    EXPECT(acr_gen1_fit_check(&b, NULL) & ACR_GEN1_FAIL_MISSING_PART);

    b = base(); b.parts.option_mask = 0x0007; b.option_cost[1] = 2;  /* 4 slots, core has 3 */
    EXPECT(acr_gen1_fit_check(&b, &n) == ACR_GEN1_FAIL_OPTION_SLOTS && n.option_slots_used == 4);
}

static void test_repair(void) {
    acr_gen1_fit_build b = base();
    EXPECT(acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_COUNT) == 0);

    /* Caterpillar: booster and listed radar go (seen on DATA LOAD). */
    b = base(); b.flags[ACR_GEN1_SLOT_LEGS] = acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_LEGS, 27);
    b.flags[ACR_GEN1_SLOT_HAND] = acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_HAND, 15);
    uint32_t r = acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_COUNT);
    EXPECT(r == ((1u << ACR_GEN1_SLOT_BOOSTER) | (1u << ACR_GEN1_SLOT_BACK_R) | (1u << ACR_GEN1_SLOT_HAND)));
    EXPECT(!b.parts.booster.present && !b.parts.back_r.present && b.parts.back_l.present);

    /* Heavy hand weapon stays on legs that allow it. */
    b = base(); b.flags[ACR_GEN1_SLOT_HAND] = ACR_GEN1_FIT_HEAVY_HAND;
    b.flags[ACR_GEN1_SLOT_LEGS] = acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_LEGS, 6);
    EXPECT(acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_COUNT) == 0);

    /* Weapon arms empty the hands. */
    b = base(); b.flags[ACR_GEN1_SLOT_ARMS] = ACR_GEN1_FIT_ARM_WEAPON;
    EXPECT(acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_ARMS) == ((1u << ACR_GEN1_SLOT_HAND) | (1u << ACR_GEN1_SLOT_BLADE)));

    /* Arms that can't carry back weapons keep the radar. */
    b = base(); b.flags[ACR_GEN1_SLOT_ARMS] = acr_gen1_fit_flags_moa(ACR_GEN1_SLOT_ARMS, 5);
    EXPECT(acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_ARMS) == (1u << ACR_GEN1_SLOT_BACK_L));

    /* Two-slot unit: fitting it empties the other slot; a loaded card keeps both. */
    b = base(); b.flags[ACR_GEN1_SLOT_BACK_L] = ACR_GEN1_FIT_TWO_SLOT;
    EXPECT(acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_COUNT) == 0);
    EXPECT(acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_BACK_L) == (1u << ACR_GEN1_SLOT_BACK_R));
    b = base(); b.flags[ACR_GEN1_SLOT_BACK_L] = ACR_GEN1_FIT_TWO_SLOT;
    EXPECT(acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_BACK_R) == (1u << ACR_GEN1_SLOT_BACK_L));
}

int main(void) {
    test_flags();
    test_check();
    test_repair();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 fit: all passed\n");
    return 0;
}
