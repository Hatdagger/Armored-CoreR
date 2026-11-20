#include "garage.h"

#include <SDL3/SDL.h>

#include <string.h>

#include "../../render/acr_render.h"

/* ---- text (one table, so it can be translated later) ---- */

/* The main menu. In a Rogue Arena run the garage is assembly only: parts
 * come from the prize screen, so no shop and no AC TEST. */
enum { M_ASSEMBLY, M_SHOP, M_TEST, M_ROGUE, M_CONTROLS, M_EXIT };
static const char *const k_menu[] = { "ASSEMBLY", "PARTS SHOP", "AC TEST", "ROGUE ARENA", "CONTROLS", "EXIT" };
static const int k_menu_main[] = { M_ASSEMBLY, M_SHOP, M_TEST, M_ROGUE, M_CONTROLS, M_EXIT };
static const int k_menu_rogue[] = { M_ASSEMBLY, M_EXIT };
static int menu_items(const garage_ui *ui, const int **ids) {
    *ids = ui->rogue ? k_menu_rogue : k_menu_main;
    return ui->rogue ? 2 : 6;
}
static const char *menu_name(const garage_ui *ui, int id) {
    return ui->rogue && id == M_EXIT ? "BACK TO THE ARENA" : k_menu[id];
}

/* Assembly rows in Master of Arena's order; -1 is the optional parts row. */
static const int k_rows[] = {
    ACR_GEN1_SLOT_HEAD, ACR_GEN1_SLOT_CORE, ACR_GEN1_SLOT_ARMS, ACR_GEN1_SLOT_LEGS,
    ACR_GEN1_SLOT_GENERATOR, ACR_GEN1_SLOT_FCS, -1, ACR_GEN1_SLOT_BOOSTER,
    ACR_GEN1_SLOT_BACK_L, ACR_GEN1_SLOT_BACK_R, ACR_GEN1_SLOT_BLADE, ACR_GEN1_SLOT_HAND
};
#define ROWS 12
static const char *const k_row_name[ROWS] = {
    "HEAD", "CORE", "ARMS", "LEGS", "GENERATOR", "FCS", "OPTIONAL", "BOOSTER",
    "BACK WEAPON L", "BACK WEAPON R", "ARM WEAPON L", "ARM WEAPON R"
};
static const char *const k_cat_name[ACR_GEN1_CAT_COUNT] = {
    "CORE", "BOOSTER", "HEAD", "ARMS", "ARM WEAPON L", "ARM WEAPON R", "BACK WEAPON",
    "GENERATOR", "FCS", "LEGS", "OPTIONAL"
};
/* Shop tabs in the assembly order. */
static const int k_shop_cats[ACR_GEN1_CAT_COUNT] = {
    ACR_GEN1_CAT_HEAD, ACR_GEN1_CAT_CORE, ACR_GEN1_CAT_ARMS, ACR_GEN1_CAT_LEGS,
    ACR_GEN1_CAT_GENERATOR, ACR_GEN1_CAT_FCS, ACR_GEN1_CAT_OPTION, ACR_GEN1_CAT_BOOSTER,
    ACR_GEN1_CAT_BACK, ACR_GEN1_CAT_BLADE, ACR_GEN1_CAT_HAND
};

static const char *result_text(acr_gen1_result r) {
    switch (r) {
    case ACR_GEN1_OK: return "";
    case ACR_GEN1_NOT_IN_SHOP: return "THE SHOP DOES NOT SELL THIS PART.";
    case ACR_GEN1_NO_CREDITS: return "NOT ENOUGH CREDITS.";
    case ACR_GEN1_IN_DEBT: return "NO TRADING WHILE IN DEBT.";
    case ACR_GEN1_STOCK_FULL: return "YOU CAN'T HOLD MORE OF THIS PART.";
    case ACR_GEN1_NONE_SPARE: return "NO SPARE COPY: UNEQUIP IT FIRST.";
    case ACR_GEN1_ALREADY_FITTED: return "ALREADY EQUIPPED.";
    case ACR_GEN1_REQUIRED: return "THIS SLOT CAN'T BE LEFT EMPTY.";
    case ACR_GEN1_NOT_ALLOWED: return "CAN'T BE EQUIPPED WITH THE CURRENT PARTS.";
    case ACR_GEN1_NO_OPTION_SLOTS: return "NOT ENOUGH OPTION SLOTS IN THE CORE.";
    }
    return "";
}

static const char *slot_short(int s) {
    static const char *const n[ACR_GEN1_SLOT_COUNT] = {
        "CORE", "BOOSTER", "HEAD", "ARMS", "ARM WEAPON L", "ARM WEAPON R", "BACK WEAPON L",
        "BACK WEAPON R", "GENERATOR", "FCS", "LEGS" };
    return n[s];
}

static void say(garage_ui *ui, const char *text) {
    SDL_strlcpy(ui->message, text, sizeof ui->message);
    ui->message_ms = 2500;
}

void garage_init(garage_ui *ui) {
    memset(ui, 0, sizeof *ui);
}

/* ---- lists ---- */

#define LIST_MAX (ACR_GEN1_PART_MAX + 1)
#define LIST_ROWS 18

/* Assembly: what can go in the current row (NO_PART first for optional slots). */
static int assembly_list(const garage_ui *ui, const garage_data *d, int out[LIST_MAX]) {
    const int s = k_rows[ui->slot];
    int n = 0;
    const acr_gen1_category cat = s < 0 ? ACR_GEN1_CAT_OPTION : acr_gen1_slot_category((acr_gen1_slot)s);
    if (s >= 0 && (s == ACR_GEN1_SLOT_BOOSTER || s == ACR_GEN1_SLOT_BLADE || s == ACR_GEN1_SLOT_HAND ||
                   s == ACR_GEN1_SLOT_BACK_L || s == ACR_GEN1_SLOT_BACK_R))
        out[n++] = ACR_GEN1_NO_PART;
    for (int i = 0; i < ACR_GEN1_PART_MAX; ++i)
        if (d->hangar->owned[cat][i] && d->cat->part[cat][i].present) out[n++] = i;
    return n;
}

static int shop_list(const garage_ui *ui, const garage_data *d, int out[LIST_MAX]) {
    const int cat = k_shop_cats[ui->shop_cat];
    int n = 0;
    for (int i = 0; i < ACR_GEN1_PART_MAX; ++i) {
        const acr_gen1_catalog_part *p = &d->cat->part[cat][i];
        if (!p->present) continue;
        if (ui->shop_sell ? d->hangar->owned[cat][i] > 0
                          : acr_gen1_shop_offers(d->hangar, d->cat, (acr_gen1_category)cat, (unsigned)i)) out[n++] = i;
    }
    return n;
}

static void move_cursor(int *cur, int *top, int n, int delta) {
    if (n <= 0) { *cur = *top = 0; return; }
    *cur = (*cur + delta + n) % n;
    if (*cur < *top) *top = *cur;
    if (*cur >= *top + LIST_ROWS) *top = *cur - LIST_ROWS + 1;
}

/* ---- input ---- */

garage_command garage_input(garage_ui *ui, garage_data *d, garage_action a) {
    int list[LIST_MAX];
    if (a == GA_NONE) return GC_NONE;
    if (ui->screen == 0) {
        const int *ids;
        const int items = menu_items(ui, &ids);
        if (ui->menu >= items) ui->menu = 0;
        if (a == GA_UP) ui->menu = (ui->menu + items - 1) % items;
        if (a == GA_DOWN) ui->menu = (ui->menu + 1) % items;
        if (a == GA_CANCEL) ui->menu = items - 1;
        if (a != GA_OK) return GC_NONE;
        const int id = ids[ui->menu];
        if (id == M_ASSEMBLY) { ui->screen = 1; ui->list_open = 0; }
        if (id == M_SHOP) { ui->screen = 2; ui->confirm = 0; ui->shop = ui->shop_top = 0; }
        if (id == M_ROGUE) return GC_ROGUE;
        if (id == M_CONTROLS) return GC_CONTROLS;
        if (id == M_TEST) {
            acr_gen1_fit_numbers n;
            const unsigned fail = acr_gen1_garage_check(d->hangar, d->cat, &n);
            if (!fail) return GC_TEST;
            say(ui, fail & ACR_GEN1_FAIL_MISSING_PART ? "AC EQUIPMENT INCOMPLETE." :
                    fail & ACR_GEN1_FAIL_NO_BOOSTER ? "THESE LEGS NEED A BOOSTER." :
                    fail & ACR_GEN1_FAIL_ENERGY ? "ENERGY OUTPUT TOO LOW." :
                    fail & ACR_GEN1_FAIL_ARM_LOAD ? "ARM LOAD OVER THE CORE'S LIMIT." :
                    fail & ACR_GEN1_FAIL_WEIGHT ? "TOO HEAVY FOR THESE LEGS." :
                    "TOO MANY OPTIONAL PARTS FOR THIS CORE.");
        }
        if (id == M_EXIT) return GC_QUIT;
        return GC_NONE;
    }

    if (ui->screen == 1) {
        if (!ui->list_open) {
            if (a == GA_UP) ui->slot = (ui->slot + ROWS - 1) % ROWS;
            if (a == GA_DOWN) ui->slot = (ui->slot + 1) % ROWS;
            if (a == GA_CANCEL) ui->screen = 0;
            if (a == GA_OK) {
                ui->list_open = 1;
                const int n = assembly_list(ui, d, list);
                const int s = k_rows[ui->slot];
                ui->list = ui->list_top = 0;
                for (int i = 0; s >= 0 && i < n; ++i)   /* start on the fitted part */
                    if (list[i] == d->hangar->fitted[s]) { ui->list = 0; move_cursor(&ui->list, &ui->list_top, n, i); }
            }
            return GC_NONE;
        }
        const int n = assembly_list(ui, d, list);
        if (a == GA_UP) move_cursor(&ui->list, &ui->list_top, n, -1);
        if (a == GA_DOWN) move_cursor(&ui->list, &ui->list_top, n, 1);
        if (a == GA_LEFT) move_cursor(&ui->list, &ui->list_top, n, -LIST_ROWS);
        if (a == GA_RIGHT) move_cursor(&ui->list, &ui->list_top, n, LIST_ROWS);
        if (a == GA_CANCEL) ui->list_open = 0;
        if (a != GA_OK || n == 0) return GC_NONE;
        const int s = k_rows[ui->slot], index = list[ui->list];
        acr_gen1_result r;
        uint32_t removed = 0;
        if (s < 0) r = acr_gen1_garage_option(d->hangar, d->cat, (unsigned)index,
                                               !((d->hangar->options >> index) & 1u));
        else r = acr_gen1_garage_fit(d->hangar, d->cat, (acr_gen1_slot)s, (unsigned)index, &removed);
        if (r != ACR_GEN1_OK) { say(ui, result_text(r)); return GC_NONE; }
        if (removed) {
            char text[96] = "UNEQUIPPED:";
            for (int k = 0; k < ACR_GEN1_SLOT_COUNT; ++k)
                if (removed & (1u << k)) { SDL_strlcat(text, " ", sizeof text); SDL_strlcat(text, slot_short(k), sizeof text); }
            say(ui, text);
        }
        return GC_CHANGED;
    }

    /* shop */
    const int n = shop_list(ui, d, list);
    if (ui->confirm) {
        if (a == GA_CANCEL) ui->confirm = 0;
        if (a != GA_OK || n == 0) return GC_NONE;
        ui->confirm = 0;
        const acr_gen1_category cat = (acr_gen1_category)k_shop_cats[ui->shop_cat];
        const acr_gen1_result r = ui->shop_sell ? acr_gen1_shop_sell(d->hangar, d->cat, cat, (unsigned)list[ui->shop])
                                                : acr_gen1_shop_buy(d->hangar, d->cat, cat, (unsigned)list[ui->shop]);
        if (r != ACR_GEN1_OK) { say(ui, result_text(r)); return GC_NONE; }
        say(ui, ui->shop_sell ? "SOLD." : "BOUGHT.");
        if (ui->shop_sell) move_cursor(&ui->shop, &ui->shop_top, shop_list(ui, d, list), 0);
        return GC_CHANGED;
    }
    if (a == GA_UP) move_cursor(&ui->shop, &ui->shop_top, n, -1);
    if (a == GA_DOWN) move_cursor(&ui->shop, &ui->shop_top, n, 1);
    if (a == GA_LEFT || a == GA_RIGHT) {
        ui->shop_cat = (ui->shop_cat + (a == GA_LEFT ? ACR_GEN1_CAT_COUNT - 1 : 1)) % ACR_GEN1_CAT_COUNT;
        ui->shop = ui->shop_top = 0;
    }
    if (a == GA_PREV_TAB || a == GA_NEXT_TAB) { ui->shop_sell = !ui->shop_sell; ui->shop = ui->shop_top = 0; }
    if (a == GA_CANCEL) ui->screen = 0;
    if (a == GA_OK && n > 0) ui->confirm = 1;
    return GC_NONE;
}

/* ---- drawing ---- */

#define PANEL_W 250.0f

void garage_view_rect(float hud_w, float *x, float *y, float *w, float *h) {
    *x = PANEL_W + 24;
    *y = 34;
    *w = hud_w - 2 * (PANEL_W + 24);
    *h = 400;
}

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define BAD 255, 90, 70
#define GOOD 120, 220, 150

static void panel(float x, float y, float w, float h) {
    acr_hud_rect(x, y, w, h, 10, 14, 20, 200, 1);
    acr_hud_rect(x, y, w, h, 70, 90, 100, 255, 0);
}

static const char *part_name(const garage_data *d, int cat, int index) {
    if (index == ACR_GEN1_NO_PART) return "NO EQUIP";
    const char *n = d->names ? d->names[cat][index] : "";
    return n[0] ? n : "?";
}

/* Price, weight and drain of a part, in the right-hand column. */
static void part_info(const garage_data *d, int cat, int index, float x, float y) {
    if (index == ACR_GEN1_NO_PART || index < 0) return;
    const acr_gen1_catalog_part *p = &d->cat->part[cat][index];
    char line[64];
    acr_hud_text(x, y, part_name(d, cat, index), HILITE);
    SDL_snprintf(line, sizeof line, "PRICE     %8u", (unsigned)p->price);
    acr_hud_text(x, y + 12, line, WHITE);
    if (cat == ACR_GEN1_CAT_OPTION) {
        SDL_snprintf(line, sizeof line, "SLOTS     %8u", p->slots);
        acr_hud_text(x, y + 24, line, WHITE);
        return;
    }
    SDL_snprintf(line, sizeof line, "WEIGHT    %8u", p->stats.weight);
    acr_hud_text(x, y + 24, line, WHITE);
    SDL_snprintf(line, sizeof line, cat == ACR_GEN1_CAT_GENERATOR ? "OUTPUT    %8u" : "EN. DRAIN %8u",
                 p->stats.energy_drain);
    acr_hud_text(x, y + 36, line, WHITE);
    SDL_snprintf(line, sizeof line, "OWNED     %5u/%2u", d->hangar->owned[cat][index],
                 acr_gen1_fitted_copies(d->hangar, (acr_gen1_category)cat, (unsigned)index));
    acr_hud_text(x, y + 48, line, DIM);
}

/* The build's totals and what keeps it from a test. */
static void totals(const garage_data *d, float x, float y) {
    acr_gen1_fit_numbers n;
    const unsigned fail = acr_gen1_garage_check(d->hangar, d->cat, &n);
    acr_gen1_fit_build b;
    acr_gen1_garage_build(d->hangar, d->cat, &b);
    const acr_gen1_build *p = &b.parts;
    const long ap = (p->head.present ? p->head.stat_2c : 0) + (p->core.present ? p->core.stat_2c : 0) +
                    (p->arms.present ? p->arms.stat_2c : 0) + (p->legs.present ? p->legs.stat_2c : 0);
    char line[64];
    acr_hud_text(x, y, "AC STATUS", HILITE);
    SDL_snprintf(line, sizeof line, "AP        %8ld", ap);
    acr_hud_text(x, y + 14, line, WHITE);
    SDL_snprintf(line, sizeof line, "WEIGHT   %5u/%5u", (unsigned)n.carried_weight, p->legs.carry_limit);
    acr_hud_text(x, y + 26, line, fail & ACR_GEN1_FAIL_WEIGHT ? 255 : 220, fail & ACR_GEN1_FAIL_WEIGHT ? 90 : 230,
                 fail & ACR_GEN1_FAIL_WEIGHT ? 70 : 235);
    SDL_snprintf(line, sizeof line, "ARM LOAD %5u/%5u", (unsigned)n.arm_load, p->core.carry_limit);
    if (fail & ACR_GEN1_FAIL_ARM_LOAD) acr_hud_text(x, y + 38, line, BAD); else acr_hud_text(x, y + 38, line, WHITE);
    SDL_snprintf(line, sizeof line, "EN. SPARE %8ld", (long)n.energy_surplus);
    if (fail & ACR_GEN1_FAIL_ENERGY) acr_hud_text(x, y + 50, line, BAD); else acr_hud_text(x, y + 50, line, WHITE);
    SDL_snprintf(line, sizeof line, "OPTIONS     %2u/%2u", (unsigned)n.option_slots_used, b.core_option_slots);
    if (fail & ACR_GEN1_FAIL_OPTION_SLOTS) acr_hud_text(x, y + 62, line, BAD); else acr_hud_text(x, y + 62, line, WHITE);
    if (!fail) acr_hud_text(x, y + 80, "READY FOR SORTIE", GOOD);
    else {
        float ly = y + 80;
        if (fail & ACR_GEN1_FAIL_MISSING_PART) { acr_hud_text(x, ly, "PARTS MISSING", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_NO_BOOSTER) { acr_hud_text(x, ly, "NO BOOSTER", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_ENERGY) { acr_hud_text(x, ly, "ENERGY SHORT", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_ARM_LOAD) { acr_hud_text(x, ly, "ARMS OVERLOADED", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_WEIGHT) { acr_hud_text(x, ly, "OVERWEIGHT", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_OPTION_SLOTS) acr_hud_text(x, ly, "OPTION SLOTS FULL", BAD);
    }
}

void garage_draw(garage_ui *ui, const garage_data *d, float hud_w, int elapsed_ms) {
    char line[96];
    const float lx = 16, rx = hud_w - PANEL_W - 16;
    static const char *const k_title[] = { "GARAGE", "ASSEMBLY", "PARTS SHOP" };
    acr_hud_text(lx, 12, k_title[ui->screen], HILITE);
    if (ui->rogue) SDL_snprintf(line, sizeof line, "BALANCE %6ld P", (long)d->hangar->credits);
    else SDL_snprintf(line, sizeof line, "CREDITS %10ld", (long)d->hangar->credits);
    if (d->hangar->credits < 0) acr_hud_text(rx + 60, 12, line, BAD); else acr_hud_text(rx + 60, 12, line, WHITE);

    panel(lx, 34, PANEL_W, 400);
    panel(rx, 34, PANEL_W, 400);
    int list[LIST_MAX];

    if (ui->screen == 0) {
        const int *ids;
        const int items = menu_items(ui, &ids);
        for (int i = 0; i < items; ++i) {
            if (i == ui->menu) { acr_hud_rect(lx + 4, 48 + i * 22 - 3, PANEL_W - 8, 14, 60, 70, 40, 255, 1); acr_hud_text(lx + 12, 48 + i * 22, menu_name(ui, ids[i]), HILITE); }
            else acr_hud_text(lx + 12, 48 + i * 22, menu_name(ui, ids[i]), WHITE);
        }
        totals(d, rx + 10, 46);
    } else if (ui->screen == 1) {
        for (int r = 0; r < ROWS; ++r) {
            const int s = k_rows[r];
            const float y = 46 + r * 26;
            const int sel = r == ui->slot;
            if (sel) acr_hud_rect(lx + 4, y - 3, PANEL_W - 8, 25, ui->list_open ? 40 : 60, ui->list_open ? 46 : 70, ui->list_open ? 30 : 40, 255, 1);
            acr_hud_text(lx + 10, y, k_row_name[r], DIM);
            if (s >= 0) {
                acr_hud_text(lx + 20, y + 11, part_name(d, acr_gen1_slot_category((acr_gen1_slot)s), d->hangar->fitted[s]),
                             sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
            } else {
                int count = 0;
                for (int b = 0; b < ACR_GEN1_OPTION_MAX; ++b) count += (d->hangar->options >> b) & 1;
                SDL_snprintf(line, sizeof line, "%d FITTED", count);
                acr_hud_text(lx + 20, y + 11, line, WHITE);
            }
        }
        if (ui->list_open) {
            /* the parts you own for this slot, over the AC view's left edge */
            const int s = k_rows[ui->slot];
            const int cat = s < 0 ? ACR_GEN1_CAT_OPTION : acr_gen1_slot_category((acr_gen1_slot)s);
            const int n = assembly_list(ui, d, list);
            const float px = lx + PANEL_W + 8;
            panel(px, 34, 230, 24 + LIST_ROWS * 12);
            acr_hud_text(px + 8, 40, k_row_name[ui->slot], HILITE);
            for (int i = ui->list_top; i < n && i < ui->list_top + LIST_ROWS; ++i) {
                const float y = 56 + (i - ui->list_top) * 12;
                const int fitted = s < 0 ? (list[i] < ACR_GEN1_OPTION_MAX && ((d->hangar->options >> list[i]) & 1))
                                         : list[i] == d->hangar->fitted[s];
                SDL_snprintf(line, sizeof line, "%c %s", fitted ? '*' : ' ', part_name(d, cat, list[i]));
                if (i == ui->list) acr_hud_rect(px + 4, y - 2, 222, 12, 60, 70, 40, 255, 1);
                acr_hud_text(px + 8, y, line, i == ui->list ? 255 : 220, i == ui->list ? 220 : 230, i == ui->list ? 120 : 235);
            }
            if (n == 0) acr_hud_text(px + 8, 56, "NO PARTS OWNED", DIM);
            totals(d, rx + 10, 46);
            if (n) part_info(d, cat, list[ui->list], rx + 10, 200);
        } else {
            totals(d, rx + 10, 46);
        }
    } else {
        const int cat = k_shop_cats[ui->shop_cat];
        const int n = shop_list(ui, d, list);
        acr_hud_text(lx + 10, 42, ui->shop_sell ? "  BUY  [SELL]" : " [BUY]  SELL ", HILITE);
        SDL_snprintf(line, sizeof line, "< %s >", k_cat_name[cat]);
        acr_hud_text(lx + 10, 56, line, WHITE);
        for (int i = ui->shop_top; i < n && i < ui->shop_top + LIST_ROWS; ++i) {
            const float y = 76 + (i - ui->shop_top) * 18;
            const acr_gen1_catalog_part *p = &d->cat->part[cat][list[i]];
            if (i == ui->shop) acr_hud_rect(lx + 4, y - 3, PANEL_W - 8, 16, 60, 70, 40, 255, 1);
            SDL_snprintf(line, sizeof line, "%-13.13s %7u %2u", part_name(d, cat, list[i]), (unsigned)p->price,
                         d->hangar->owned[cat][list[i]]);
            acr_hud_text(lx + 8, y, line, i == ui->shop ? 255 : 220, i == ui->shop ? 220 : 230, i == ui->shop ? 120 : 235);
        }
        if (n == 0) acr_hud_text(lx + 10, 76, ui->shop_sell ? "NOTHING TO SELL" : "SHOP EMPTY: SELL TO STOCK IT", DIM);
        totals(d, rx + 10, 46);
        if (n) part_info(d, cat, list[ui->shop], rx + 10, 200);
        if (ui->confirm && n) {
            const acr_gen1_catalog_part *p = &d->cat->part[cat][list[ui->shop]];
            const uint32_t price = ui->shop_sell ? (uint32_t)((uint64_t)p->price * d->cat->sell_percent / 100) : p->price;
            SDL_snprintf(line, sizeof line, "%s %s FOR %u CREDITS?", ui->shop_sell ? "SELL" : "BUY",
                         part_name(d, cat, list[ui->shop]), (unsigned)price);
            const float w = (float)SDL_strlen(line) * 8 + 24;
            panel(hud_w / 2 - w / 2, 200, w, 44);
            acr_hud_text(hud_w / 2 - w / 2 + 12, 210, line, HILITE);
            acr_hud_text(hud_w / 2 - 60, 226, "A: YES   B: NO", WHITE);
        }
    }

    if (ui->message_ms > 0) {
        ui->message_ms -= elapsed_ms;
        acr_hud_text(lx, 442, ui->message, HILITE);
    }
    static const char *const k_hints[] = {
        "A: SELECT   B: BACK",
        "A: EQUIP   B: BACK   LEFT/RIGHT: PAGE",
        "A: BUY/SELL   B: BACK   LEFT/RIGHT: CATEGORY   LB/RB: BUY OR SELL" };
    const int hint = ui->screen == 1 ? 1 : ui->screen;
    acr_hud_text(lx, 460, k_hints[hint], DIM);
}
