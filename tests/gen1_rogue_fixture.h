/* Made-up parts, prices and opponents for the Rogue Arena tests (solo and
 * co-op): no game data in the repo; the fitting flags are the Master of
 * Arena index rules. */
#ifndef GEN1_ROGUE_FIXTURE_H
#define GEN1_ROGUE_FIXTURE_H

#include <stdio.h>
#include <string.h>

#include "acr_gen1_rogue.h"

static acr_gen1_catalog g_cat;
static acr_gen1_rogue_parts g_info;
static acr_gen1_rogue_foe g_foe[80];
static int g_foes;

static void add(acr_gen1_category cat, acr_gen1_slot slot, unsigned index, uint32_t price, uint16_t weight) {
    acr_gen1_catalog_part *p = &g_cat.part[cat][index];
    memset(p, 0, sizeof *p);
    p->present = p->for_sale = 1;
    p->price = price;
    p->stats.present = 1;
    p->stats.weight = weight;
    p->stats.energy_drain = 10;
    p->flags = slot < ACR_GEN1_SLOT_COUNT ? acr_gen1_fit_flags_moa(slot, index) : 0;
}

/* Prices spread over the P tiers (1..7). */
static uint32_t price_of(unsigned i) {
    static const uint32_t k[7] = { 15000, 25000, 40000, 55000, 75000, 100000, 200000 };
    return k[i % 7];
}

static void make_catalog(void) {
    memset(&g_cat, 0, sizeof g_cat);
    memset(&g_info, 0, sizeof g_info);
    g_cat.sell_percent = 100;
    for (unsigned i = 0; i < 8; ++i) {
        add(ACR_GEN1_CAT_CORE, ACR_GEN1_SLOT_CORE, i, price_of(i), 100);
        g_cat.part[ACR_GEN1_CAT_CORE][i].stats.carry_limit = 30000;
        g_cat.part[ACR_GEN1_CAT_CORE][i].slots = 4;
        add(ACR_GEN1_CAT_HEAD, ACR_GEN1_SLOT_HEAD, i, price_of(i + 1), 100);
        add(ACR_GEN1_CAT_BOOSTER, ACR_GEN1_SLOT_BOOSTER, i, price_of(i + 2), 100);
        add(ACR_GEN1_CAT_FCS, ACR_GEN1_SLOT_FCS, i, price_of(i + 3), 50);
        add(ACR_GEN1_CAT_BLADE, ACR_GEN1_SLOT_BLADE, i % 5, price_of(i + 4), 50);
        add(ACR_GEN1_CAT_GENERATOR, ACR_GEN1_SLOT_GENERATOR, i, price_of(i + 5), 100);
        g_cat.part[ACR_GEN1_CAT_GENERATOR][i].stats.energy_drain = 60000;   /* output */
    }
    for (unsigned i = 0; i < 16; ++i) {                /* regular arms 0-11, weapon arms 12-15 */
        add(ACR_GEN1_CAT_ARMS, ACR_GEN1_SLOT_ARMS, i, price_of(i + 2), 100);
        g_info.attack[ACR_GEN1_CAT_ARMS][i] = i >= 12 ? 500 : 0;
    }
    for (unsigned i = 1; i < 20; ++i) {
        add(ACR_GEN1_CAT_HAND, ACR_GEN1_SLOT_HAND, i, price_of(i), 50);
        g_info.attack[ACR_GEN1_CAT_HAND][i] = 100;
    }
    for (unsigned i = 0; i < 33; ++i) {                /* every leg type */
        add(ACR_GEN1_CAT_LEGS, ACR_GEN1_SLOT_LEGS, i, price_of(i + 6), 200);
        g_cat.part[ACR_GEN1_CAT_LEGS][i].stats.carry_limit = 30000;
    }
    for (unsigned i = 0; i < 58; ++i) {
        add(ACR_GEN1_CAT_BACK, ACR_GEN1_SLOT_BACK_L, i, price_of(i), 50);
        if (i >= 40 && i <= 47) g_info.flags[ACR_GEN1_CAT_BACK][i] |= ACR_GEN1_ROGUE_RADAR;
        if (i == 57) g_info.flags[ACR_GEN1_CAT_BACK][i] |= ACR_GEN1_ROGUE_STEALTH;
        if (i >= 28 && i <= 36) g_info.flags[ACR_GEN1_CAT_BACK][i] |= ACR_GEN1_ROGUE_STANCE;
    }
    for (unsigned b = 0; b < 12; ++b) {
        add(ACR_GEN1_CAT_OPTION, ACR_GEN1_SLOT_COUNT, b, 20000, 0);
        g_cat.part[ACR_GEN1_CAT_OPTION][b].slots = 1;
    }
}

static void make_roster(void) {
    static const uint8_t k_entries[9] = { 50, 10, 10, 10, 10, 16, 31, 10, 20 };
    g_foes = 0;
    uint64_t s = 7;
    for (int a = 0; a < 9 && g_foes < 80; ++a)
        for (int r = 1; r < k_entries[a] && r <= 9 && g_foes < 80; ++r) {
            acr_gen1_rogue_foe *f = &g_foe[g_foes++];
            memset(f, 0, sizeof *f);
            f->arena = (uint8_t)a; f->rank = (uint8_t)r; f->entries = k_entries[a];
            for (int k = 0; k < ACR_GEN1_SLOT_COUNT; ++k) f->parts[k] = 0;
            f->parts[ACR_GEN1_SLOT_ARMS] = 1;
            f->parts[ACR_GEN1_SLOT_HAND] = 1;
            f->parts[ACR_GEN1_SLOT_BACK_R] = 1;
            f->karasawa = acr_gen1_rogue_below(&s, 8) == 0;
            acr_gen1_ai_preset(&f->ai, ACR_GEN1_AI_BALANCED);
            f->ai.rebel = acr_gen1_rogue_below(&s, 7) == 0 ? 9 : 0;
            snprintf(f->pilot, sizeof f->pilot, "P%d-%d", a, r);
            snprintf(f->ac, sizeof f->ac, "AC%d-%d", a, r);
        }
}

static acr_gen1_rogue_world world(void) {
    acr_gen1_rogue_world w = { g_foe, g_foes, &g_cat, &g_info };
    return w;
}

#endif
