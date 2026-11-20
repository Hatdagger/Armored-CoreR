/* Gen 1 garage and shop. Made-up parts and prices only (no game data in
 * the repo); the fitting flags are the Master of Arena index rules. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_garage.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_catalog g_cat;

static void add(acr_gen1_category cat, acr_gen1_slot slot, unsigned index, uint32_t price,
                uint16_t weight, uint16_t drain) {
    acr_gen1_catalog_part *p = &g_cat.part[cat][index];
    memset(p, 0, sizeof *p);
    p->present = p->for_sale = 1;
    p->price = price;
    p->stats.present = 1;
    p->stats.weight = weight;
    p->stats.energy_drain = drain;
    p->flags = acr_gen1_fit_flags_moa(slot, index);
}

static void make_catalog(void) {
    memset(&g_cat, 0, sizeof g_cat);
    g_cat.sell_percent = 100;
    for (unsigned i = 0; i < 3; ++i) add(ACR_GEN1_CAT_CORE, ACR_GEN1_SLOT_CORE, i, 1000 + i, 900, 300);
    for (unsigned i = 0; i < 3; ++i) g_cat.part[ACR_GEN1_CAT_CORE][i].stats.carry_limit = 3000;
    g_cat.part[ACR_GEN1_CAT_CORE][0].slots = 2;
    g_cat.part[ACR_GEN1_CAT_CORE][1].slots = 4;
    add(ACR_GEN1_CAT_BOOSTER, ACR_GEN1_SLOT_BOOSTER, 0, 500, 100, 100);
    add(ACR_GEN1_CAT_HEAD, ACR_GEN1_SLOT_HEAD, 0, 700, 150, 100);
    add(ACR_GEN1_CAT_ARMS, ACR_GEN1_SLOT_ARMS, 1, 800, 700, 200);
    add(ACR_GEN1_CAT_ARMS, ACR_GEN1_SLOT_ARMS, 12, 2000, 900, 300);   /* weapon arms */
    add(ACR_GEN1_CAT_BLADE, ACR_GEN1_SLOT_BLADE, 0, 300, 100, 10);
    add(ACR_GEN1_CAT_HAND, ACR_GEN1_SLOT_HAND, 1, 400, 300, 10);
    add(ACR_GEN1_CAT_BACK, ACR_GEN1_SLOT_BACK_L, 0, 600, 300, 50);
    add(ACR_GEN1_CAT_BACK, ACR_GEN1_SLOT_BACK_L, 40, 250, 100, 50);  /* radar */
    add(ACR_GEN1_CAT_BACK, ACR_GEN1_SLOT_BACK_L, 52, 900, 500, 50);  /* two-slot */
    add(ACR_GEN1_CAT_GENERATOR, ACR_GEN1_SLOT_GENERATOR, 0, 900, 400, 5000);
    add(ACR_GEN1_CAT_FCS, ACR_GEN1_SLOT_FCS, 0, 200, 50, 50);
    add(ACR_GEN1_CAT_LEGS, ACR_GEN1_SLOT_LEGS, 3, 1500, 1800, 1200);
    g_cat.part[ACR_GEN1_CAT_LEGS][3].stats.carry_limit = 5000;
    add(ACR_GEN1_CAT_LEGS, ACR_GEN1_SLOT_LEGS, 27, 3000, 2500, 1500); /* caterpillars */
    g_cat.part[ACR_GEN1_CAT_LEGS][27].stats.carry_limit = 8000;
    for (unsigned b = 0; b < 3; ++b) {
        add(ACR_GEN1_CAT_OPTION, ACR_GEN1_SLOT_COUNT, b, 100 * (b + 1), 0, 0);
        g_cat.part[ACR_GEN1_CAT_OPTION][b].slots = (uint8_t)(b + 1);
    }
    g_cat.part[ACR_GEN1_CAT_ARMS][12].for_sale = 0;   /* a prize part: not in the shop */
}

/* core 0, booster 0, head 0, arms 1, blade 0, hand 1, back 0 + radar 40, gen 0, fcs 0, legs 3 */
static const uint8_t k_start[ACR_GEN1_SLOT_COUNT] = { 0, 0, 0, 1, 0, 1, 0, 40, 0, 0, 3 };

static void test_new_and_shop(void) {
    acr_gen1_hangar h;
    acr_gen1_hangar_new(&h, k_start, 0, 0);
    EXPECT(h.owned[ACR_GEN1_CAT_BACK][0] == 1 && h.owned[ACR_GEN1_CAT_BACK][40] == 1);
    EXPECT(acr_gen1_fitted_copies(&h, ACR_GEN1_CAT_LEGS, 3) == 1);
    EXPECT(acr_gen1_spare_copies(&h, ACR_GEN1_CAT_LEGS, 3) == 0);
    EXPECT(acr_gen1_garage_check(&h, &g_cat, NULL) == 0);

    /* No money: can't buy. In debt a fitted frame part can't be sold
     * ("Pay back your debt first"). */
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_CORE, 1) == ACR_GEN1_NO_CREDITS);
    h.credits = -1;
    EXPECT(acr_gen1_shop_sell(&h, &g_cat, ACR_GEN1_CAT_LEGS, 3) == ACR_GEN1_IN_DEBT);
    h.credits = 0;
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_ARMS, 12) == ACR_GEN1_NOT_IN_SHOP);
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_ARMS, 7) == ACR_GEN1_NOT_IN_SHOP);

    /* Empty the hand, sell the weapon at full price, buy a core with it. */
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_HAND, ACR_GEN1_NO_PART, NULL) == ACR_GEN1_OK);
    EXPECT(acr_gen1_shop_sell(&h, &g_cat, ACR_GEN1_CAT_HAND, 1) == ACR_GEN1_OK);
    EXPECT(h.credits == 400 && h.owned[ACR_GEN1_CAT_HAND][1] == 0);
    h.credits = 1500;
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_CORE, 1) == ACR_GEN1_OK);
    EXPECT(h.credits == 499 && h.owned[ACR_GEN1_CAT_CORE][1] == 1);
    /* Sell price follows the catalogue's percentage. */
    g_cat.sell_percent = 50;
    EXPECT(acr_gen1_shop_sell(&h, &g_cat, ACR_GEN1_CAT_CORE, 1) == ACR_GEN1_OK);
    EXPECT(h.credits == 499 + 500);
    g_cat.sell_percent = 100;
    /* In debt: buying needs the credits; weapons still sell, even fitted
     * (the slot is emptied), as in the game's shop. */
    h.credits = -1;
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_FCS, 0) == ACR_GEN1_NO_CREDITS);
    EXPECT(acr_gen1_shop_sell(&h, &g_cat, ACR_GEN1_CAT_BLADE, 0) == ACR_GEN1_OK);
    EXPECT(h.fitted[ACR_GEN1_SLOT_BLADE] == ACR_GEN1_NO_PART);
    /* Out of debt a fitted frame part sells too, leaving the slot empty. */
    h.credits = 0;
    EXPECT(acr_gen1_shop_sell(&h, &g_cat, ACR_GEN1_CAT_LEGS, 3) == ACR_GEN1_OK);
    EXPECT(h.fitted[ACR_GEN1_SLOT_LEGS] == ACR_GEN1_NO_PART);
    EXPECT(acr_gen1_garage_check(&h, &g_cat, NULL) & ACR_GEN1_FAIL_MISSING_PART);
    /* Stock limit. */
    h.credits = 1000000;
    h.owned[ACR_GEN1_CAT_FCS][0] = ACR_GEN1_OWN_MAX;
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_FCS, 0) == ACR_GEN1_STOCK_FULL);
}

static void test_fit(void) {
    acr_gen1_hangar h;
    acr_gen1_hangar_new(&h, k_start, 0, 100000);
    uint32_t off = 0;
    /* Needs a spare copy. */
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_CORE, 1, &off) == ACR_GEN1_NONE_SPARE);
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_CORE, 0, &off) == ACR_GEN1_ALREADY_FITTED);
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_HEAD, ACR_GEN1_NO_PART, &off) == ACR_GEN1_REQUIRED);
    /* Swapping cores frees the old one for sale. */
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_CORE, 1) == ACR_GEN1_OK);
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_CORE, 1, &off) == ACR_GEN1_OK && off == 0);
    EXPECT(acr_gen1_spare_copies(&h, ACR_GEN1_CAT_CORE, 0) == 1);

    /* Weapon arms take the hand weapon and blade off ... */
    h.owned[ACR_GEN1_CAT_ARMS][12] = 1;
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_ARMS, 12, &off) == ACR_GEN1_OK);
    EXPECT(off == ((1u << ACR_GEN1_SLOT_HAND) | (1u << ACR_GEN1_SLOT_BLADE)));
    EXPECT(h.fitted[ACR_GEN1_SLOT_HAND] == ACR_GEN1_NO_PART);
    /* ... and a hand weapon is then refused, leaving the build as it was. */
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_HAND, 1, &off) == ACR_GEN1_NOT_ALLOWED);
    EXPECT(h.fitted[ACR_GEN1_SLOT_HAND] == ACR_GEN1_NO_PART && h.fitted[ACR_GEN1_SLOT_ARMS] == 12);
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_ARMS, 1, &off) == ACR_GEN1_OK);

    /* Caterpillars drop the booster and the radar; a booster is then refused. */
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_LEGS, 27) == ACR_GEN1_OK);
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_LEGS, 27, &off) == ACR_GEN1_OK);
    EXPECT(off == ((1u << ACR_GEN1_SLOT_BOOSTER) | (1u << ACR_GEN1_SLOT_BACK_R)));
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_BOOSTER, 0, &off) == ACR_GEN1_NOT_ALLOWED);
    EXPECT(acr_gen1_garage_check(&h, &g_cat, NULL) == 0);   /* caterpillars need no booster */
    /* Leaving caterpillars fits the first owned booster again. */
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_LEGS, 3, &off) == ACR_GEN1_OK);
    EXPECT(h.fitted[ACR_GEN1_SLOT_BOOSTER] == 0);
    EXPECT(acr_gen1_garage_check(&h, &g_cat, NULL) == 0);

    /* A two-slot unit empties the other back slot; the same part twice needs two copies. */
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_BACK_R, 0, &off) == ACR_GEN1_NONE_SPARE);
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_BACK, 0) == ACR_GEN1_OK);
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_BACK_R, 0, &off) == ACR_GEN1_OK);
    EXPECT(acr_gen1_fitted_copies(&h, ACR_GEN1_CAT_BACK, 0) == 2);
    EXPECT(acr_gen1_shop_buy(&h, &g_cat, ACR_GEN1_CAT_BACK, 52) == ACR_GEN1_OK);
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_BACK_L, 52, &off) == ACR_GEN1_OK);
    EXPECT(off == (1u << ACR_GEN1_SLOT_BACK_R) && h.fitted[ACR_GEN1_SLOT_BACK_R] == ACR_GEN1_NO_PART);

    /* Options: core 1 has 4 slots; options 0 (1 slot) and 2 (3 slots) fit, then no room. */
    h.owned[ACR_GEN1_CAT_OPTION][0] = h.owned[ACR_GEN1_CAT_OPTION][1] = h.owned[ACR_GEN1_CAT_OPTION][2] = 1;
    EXPECT(acr_gen1_garage_option(&h, &g_cat, 0, 1) == ACR_GEN1_OK);
    EXPECT(acr_gen1_garage_option(&h, &g_cat, 2, 1) == ACR_GEN1_OK);
    EXPECT(acr_gen1_garage_option(&h, &g_cat, 1, 1) == ACR_GEN1_NO_OPTION_SLOTS);
    EXPECT(acr_gen1_garage_option(&h, &g_cat, 2, 0) == ACR_GEN1_OK);
    EXPECT(acr_gen1_garage_option(&h, &g_cat, 1, 1) == ACR_GEN1_OK);
    /* A core with fewer slots keeps the options in bit order while they
     * fit; the first that doesn't and every one after it come off. */
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_CORE, 0, &off) == ACR_GEN1_OK);
    EXPECT(!(acr_gen1_garage_check(&h, &g_cat, NULL) & ACR_GEN1_FAIL_OPTION_SLOTS));
    EXPECT((h.options & 1u) && !(h.options & 2u));
    /* Selling a fitted option takes it off. */
    EXPECT(acr_gen1_shop_sell(&h, &g_cat, ACR_GEN1_CAT_OPTION, 0) == ACR_GEN1_OK);
    EXPECT(!(h.options & 1u));
}

static void test_save(void) {
    acr_gen1_hangar h;
    acr_gen1_hangar_new(&h, k_start, 0x0005, -1234);
    strcpy(h.ac_name, "TEST \"AC\"");
    strcpy(h.pilot_name, "PILOT");
    h.owned[ACR_GEN1_CAT_HEAD][0] = 3;
    h.colours[3][2] = 63;
    h.fitted[ACR_GEN1_SLOT_BLADE] = ACR_GEN1_NO_PART;
    h.stock[ACR_GEN1_CAT_ARMS][12] = 2;
    char buf[4096];
    const size_t need = acr_gen1_save_write(&h, buf, sizeof buf);
    EXPECT(need < sizeof buf && strlen(buf) == need);
    EXPECT(acr_gen1_save_write(&h, NULL, 0) == need);
    char tiny[16];
    EXPECT(acr_gen1_save_write(&h, tiny, sizeof tiny) == need && strlen(tiny) == sizeof tiny - 1);

    acr_gen1_hangar r;
    memset(&r, 0x5A, sizeof r);
    EXPECT(acr_gen1_save_read(&r, buf));
    EXPECT(r.credits == -1234);
    EXPECT(!strcmp(r.ac_name, "TEST _AC_") && !strcmp(r.pilot_name, "PILOT"));
    EXPECT(!memcmp(r.fitted, h.fitted, sizeof r.fitted) && r.options == 0x0005);
    EXPECT(!memcmp(r.owned, h.owned, sizeof r.owned));
    EXPECT(!memcmp(r.stock, h.stock, sizeof r.stock));
    EXPECT(r.colours[3][2] == 63);

    /* Rejected: a newer format, another generation, bad numbers. */
    acr_gen1_hangar keep = r;
    EXPECT(!acr_gen1_save_read(&r, "format = 3\ngeneration = \"gen1\"\n"));
    EXPECT(!acr_gen1_save_read(&r, "format = 1\ngeneration = \"gen2\"\n"));
    EXPECT(!acr_gen1_save_read(&r, "format = 1\ngeneration = \"gen1\"\n[owned]\ncore = [0, 99x]\n"));
    EXPECT(!acr_gen1_save_read(&r, "format = 1\ngeneration = \"gen1\"\n[ac]\ncore = 64\n"));
    EXPECT(!memcmp(&r, &keep, sizeof r));
    /* A hand-edited file that fits a part it doesn't list as owned owns it. */
    EXPECT(acr_gen1_save_read(&r, "format = 1\ngeneration = \"gen1\"\ncredits = 5\n[ac]\nlegs = 9\n"));
    EXPECT(r.credits == 5 && r.fitted[ACR_GEN1_SLOT_LEGS] == 9 && r.owned[ACR_GEN1_CAT_LEGS][9] == 1);
    EXPECT(r.fitted[ACR_GEN1_SLOT_CORE] == ACR_GEN1_NO_PART);
    /* Format 1 (no [shop]) still reads, with an empty shop stock. */
    EXPECT(acr_gen1_save_read(&r, "format = 1\ngeneration = \"gen1\"\n[owned]\nhead = [0]\n"));
    EXPECT(r.owned[ACR_GEN1_CAT_HEAD][0] == 1 && r.stock[ACR_GEN1_CAT_ARMS][12] == 0);
}

/* Test builds: every part owned, nothing for sale; sold parts can be bought back. */
static void test_test_kit(void) {
    acr_gen1_catalog shop_closed = g_cat;
    for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c)
        for (int i = 0; i < ACR_GEN1_PART_MAX; ++i) shop_closed.part[c][i].for_sale = 0;
    acr_gen1_hangar h;
    acr_gen1_hangar_new(&h, k_start, 0, 0);
    acr_gen1_hangar_give_all(&h, &shop_closed);
    EXPECT(h.owned[ACR_GEN1_CAT_LEGS][27] == 1 && h.owned[ACR_GEN1_CAT_ARMS][12] == 1);
    EXPECT(h.owned[ACR_GEN1_CAT_OPTION][2] == 1 && h.owned[ACR_GEN1_CAT_LEGS][3] == 1);
    EXPECT(!acr_gen1_shop_offers(&h, &shop_closed, ACR_GEN1_CAT_LEGS, 27));
    EXPECT(acr_gen1_shop_buy(&h, &shop_closed, ACR_GEN1_CAT_LEGS, 27) == ACR_GEN1_NOT_IN_SHOP);
    EXPECT(acr_gen1_shop_sell(&h, &shop_closed, ACR_GEN1_CAT_LEGS, 27) == ACR_GEN1_OK);
    EXPECT(h.credits == 3000 && h.stock[ACR_GEN1_CAT_LEGS][27] == 1);
    EXPECT(acr_gen1_shop_offers(&h, &shop_closed, ACR_GEN1_CAT_LEGS, 27));
    EXPECT(acr_gen1_shop_buy(&h, &shop_closed, ACR_GEN1_CAT_LEGS, 27) == ACR_GEN1_OK);
    EXPECT(h.credits == 0 && h.stock[ACR_GEN1_CAT_LEGS][27] == 0 && h.owned[ACR_GEN1_CAT_LEGS][27] == 1);
    EXPECT(acr_gen1_shop_buy(&h, &shop_closed, ACR_GEN1_CAT_LEGS, 27) == ACR_GEN1_NOT_IN_SHOP);
}

/* SP-MODERN: owned from a new game, takes every option slot, fits any core. */
static void test_modern(void) {
    acr_gen1_catalog_add_own_parts(&g_cat, NULL);
    acr_gen1_hangar h;
    acr_gen1_hangar_new(&h, k_start, 0, 0);
    const unsigned m = ACR_GEN1_OPTION_MODERN_BIT;
    EXPECT(h.owned[ACR_GEN1_CAT_OPTION][m] == 1 && !(h.options & ACR_GEN1_OPTION_MODERN));
    h.owned[ACR_GEN1_CAT_OPTION][0] = 1;
    EXPECT(acr_gen1_garage_option(&h, &g_cat, 0, 1) == ACR_GEN1_OK);          /* 1 of core 0's 2 slots */
    EXPECT(acr_gen1_garage_option(&h, &g_cat, m, 1) == ACR_GEN1_OK);          /* takes option 0 off */
    EXPECT(h.options == ACR_GEN1_OPTION_MODERN);
    EXPECT(acr_gen1_human_plus_level(9, h.options) == 0 && acr_gen1_human_plus_level(9, 1) == 9);
    EXPECT(acr_gen1_garage_option(&h, &g_cat, 0, 1) == ACR_GEN1_NO_OPTION_SLOTS);
    acr_gen1_fit_numbers n;
    EXPECT(!(acr_gen1_garage_check(&h, &g_cat, &n) & ACR_GEN1_FAIL_OPTION_SLOTS) && n.option_slots_used == 2);
    /* A core with more slots: still all of them, and still alone. */
    h.owned[ACR_GEN1_CAT_CORE][1] = 1;
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_CORE, 1, NULL) == ACR_GEN1_OK);
    EXPECT(h.options & ACR_GEN1_OPTION_MODERN);
    EXPECT(acr_gen1_garage_option(&h, &g_cat, 0, 1) == ACR_GEN1_NO_OPTION_SLOTS);
    /* A core without slots takes it too. */
    h.owned[ACR_GEN1_CAT_CORE][2] = 1;
    EXPECT(acr_gen1_garage_fit(&h, &g_cat, ACR_GEN1_SLOT_CORE, 2, NULL) == ACR_GEN1_OK);
    EXPECT(h.options & ACR_GEN1_OPTION_MODERN);
    EXPECT(!(acr_gen1_garage_check(&h, &g_cat, &n) & ACR_GEN1_FAIL_OPTION_SLOTS));
    /* It saves and loads like any option. */
    char text[4096];
    acr_gen1_save_write(&h, text, sizeof text);
    acr_gen1_hangar r;
    EXPECT(acr_gen1_save_read(&r, text) && (r.options & ACR_GEN1_OPTION_MODERN) && r.owned[ACR_GEN1_CAT_OPTION][m] == 1);
    memset(&g_cat.part[ACR_GEN1_CAT_OPTION][m], 0, sizeof g_cat.part[0][0]);
}

int main(void) {
    make_catalog();
    test_modern();
    test_new_and_shop();
    test_fit();
    test_save();
    test_test_kit();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 garage: all passed\n");
    return 0;
}
