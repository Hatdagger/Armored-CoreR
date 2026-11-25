#include "missions.h"

#include <SDL3/SDL.h>

#include <string.h>

#include "../../model/acr_png.h"
#include "../../render/acr_render.h"
#include "sfx.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define GOOD 120, 230, 140
#define BAD 255, 90, 70

enum { MS_LIST, MS_BRIEF, MS_ASK, MS_REPORT };

#define LIST_ROWS 14
#define TEXT_SIZE 12.0f

typedef struct tex { uint32_t id; int w, h; } tex;

static struct {
    char import_dir[1024];
    acr_gen1_hangar *h;
    const acr_gen1_catalog *cat;
    acr_gen1_mission_list list;
    int moa;                        /* MoA missions read from the import */
    int screen, cur, top;
    int page, ask, view_only;
    tex pic[ACR_GEN1_MISSION_PAGES];
    int pic_for;                    /* the list row whose pictures are loaded, -1 none */
    tex report[3], digits, digits_red;
    int report_pic;                 /* 0..2 = RTIM 63..65 */
    int report_ms;
    acr_gen1_mission_report rep;
    int fight_row;
    rogue_fight fight;
    char message[96];
    int message_ms, err, snd;
} M;

static acr_gen1_mission_record g_record;
acr_gen1_mission_record *missions_record(void) { return &g_record; }

static void say(const char *t) {
    SDL_strlcpy(M.message, t, sizeof M.message);
    M.message_ms = 2500;
    M.err = 1;
}

/* 1234567 -> "1,234,567" */
static void num(long v, char *out, size_t cap) {
    char t[32];
    const int neg = v < 0;
    unsigned long u = (unsigned long)(neg ? -v : v);
    int n = 0, k = 0;
    do { if (k && k % 3 == 0) t[n++] = ','; t[n++] = (char)('0' + u % 10u); u /= 10u; ++k; } while (u && n < 28);
    if (neg) t[n++] = '-';
    size_t o = 0;
    for (int i = n - 1; i >= 0 && o + 1 < cap; --i) out[o++] = t[i];
    out[o] = 0;
}

/* ---- pictures ---- */

static tex load_png(const char *path) {
    tex t = { 0, 0, 0 };
    size_t size = 0;
    uint8_t *data = SDL_LoadFile(path, &size);
    if (!data) return t;
    acr_image img;
    const int ok = acr_png_decode(data, size, &img);
    SDL_free(data);
    if (!ok) return t;
    uint8_t *rgba = SDL_malloc((size_t)img.w * img.h * 4);
    if (rgba) {
        if (img.indexed) for (int i = 0; i < img.w * img.h; ++i) SDL_memcpy(rgba + 4 * i, img.palette[img.pixels[i]], 4);
        else SDL_memcpy(rgba, img.pixels, (size_t)img.w * img.h * 4);
        t.id = acr_render_texture(rgba, img.w, img.h, 1);
        t.w = img.w;
        t.h = img.h;
        SDL_free(rgba);
    }
    acr_image_free(&img);
    return t;
}

static void load_pictures(int row) {
    if (M.pic_for == row) return;
    SDL_memset(M.pic, 0, sizeof M.pic);   /* the old textures stay with the renderer; few and small */
    M.pic_for = row;
    const acr_gen1_mission_info *m = &M.list.m[row];
    if (m->own || SDL_strncmp(m->id, "MOA-", 4)) return;
    const int slot = SDL_atoi(m->id + 4);
    for (int k = 0; k < m->pictures && k < ACR_GEN1_MISSION_PAGES; ++k) {
        char path[1300];
        SDL_snprintf(path, sizeof path, "%s/missions/%02d-pic-%d.png", M.import_dir, slot, k);
        M.pic[k] = load_png(path);
    }
}

/* ---- setup ---- */

void missions_init(const char *import_dir, acr_gen1_hangar *hangar, const acr_gen1_catalog *cat) {
    SDL_strlcpy(M.import_dir, import_dir, sizeof M.import_dir);
    M.h = hangar;
    M.cat = cat;
    char path[1300];
    SDL_snprintf(path, sizeof path, "%s/missions/missions.toml", import_dir);
    char *t = SDL_LoadFile(path, NULL);
    M.moa = acr_gen1_missions_build(&M.list, t);
    SDL_free(t);
    for (int k = 0; k < 3; ++k) {
        SDL_snprintf(path, sizeof path, "%s/missions/report-%d.png", import_dir, 63 + k);
        M.report[k] = load_png(path);
    }
    SDL_snprintf(path, sizeof path, "%s/missions/report-digits.png", import_dir);
    M.digits = load_png(path);
    SDL_snprintf(path, sizeof path, "%s/missions/report-digits-red.png", import_dir);
    M.digits_red = load_png(path);
    M.pic_for = -1;
    M.screen = MS_LIST;
}

void missions_open(void) {
    M.screen = MS_LIST;
    M.cur = M.top = 0;
    M.message[0] = 0;
    M.message_ms = 0;
}

/* ---- sortie ---- */

const rogue_fight *missions_fight_info(void) { return &M.fight; }
int32_t missions_round_ms(void) { return M.list.m[M.fight_row].time_ms; }

static int prepare_sortie(void) {
    const acr_gen1_mission_info *m = &M.list.m[M.cur];
    if (!m->playable || !M.h) return 0;
    rogue_fight *g = &M.fight;
    SDL_zero(*g);
    /* P4's examiner: the mockup gives a "balanced mid-weight build, no
     * Human PLUS" but no parts yet, so it flies the player's own build, as
     * AC TEST's target does (main.c gives it the balanced AI). */
    for (int s = 0; s < SLOT_COUNT; ++s) g->parts[0][s] = g->parts[1][s] = M.h->fitted[s];
    memcpy(g->colours[0], M.h->colours, sizeof g->colours[0]);
    memcpy(g->colours[1], M.h->colours, sizeof g->colours[1]);
    g->stage = m->map;
    SDL_snprintf(g->name[0], sizeof g->name[0], "%s / %s", M.h->ac_name, M.h->pilot_name);
    SDL_strlcpy(g->name[1], "NEST EXAMINER", sizeof g->name[1]);
    M.fight_row = M.cur;
    return 1;
}

int missions_fight_over(int result, int32_t ap_left, int32_t ap_max, int32_t ammo_cost) {
    const acr_gen1_mission_info *m = &M.list.m[M.fight_row];
    const int success = result == 1;
    const int32_t price = M.h && M.cat ? acr_gen1_mission_ac_price(M.h, M.cat) : 0;
    const int32_t repair = acr_gen1_mission_repair_cost(ap_max, ap_left, price);
    if (M.h) acr_gen1_mission_settle(M.h, &M.rep, success, m->reward, 0, ammo_cost, repair, 0);
    acr_gen1_mission_book(&g_record, m->id, success);
    /* The report's picture: 63 or 64 after a success (the game picks one at
     * random, rand() <= 0x4000 -> 64), 65 after a failure. */
    M.report_pic = success ? (SDL_rand(2) ? 1 : 0) : 2;
    M.report_ms = 0;
    M.cur = M.fight_row;
    M.screen = MS_REPORT;
    return 1;
}

/* ---- input ---- */

static int step(int cur, int n, garage_action a) {
    if (n <= 0) return 0;
    if (a == GA_UP) return (cur + n - 1) % n;
    if (a == GA_DOWN) return (cur + 1) % n;
    return cur;
}

static void scroll_to_cursor(void) {
    if (M.cur < M.top) M.top = M.cur;
    if (M.cur >= M.top + LIST_ROWS) M.top = M.cur - LIST_ROWS + 1;
}

static int brief_pages(void) { return 1 + M.list.m[M.cur].pages; }   /* the mail header, then the text */

static missions_command input(garage_action a) {
    acr_gen1_mission_info *m = &M.list.m[M.cur];
    switch (M.screen) {
    case MS_LIST:
        if (a == GA_UP || a == GA_DOWN) { M.cur = step(M.cur, M.list.count, a); scroll_to_cursor(); return MC_NONE; }
        if (a == GA_CANCEL) return MC_LEAVE;
        /* Triangle / Tab reads a briefing that can't be flown yet. */
        if (a == GA_INFO || (a == GA_OK && m->playable)) {
            M.view_only = !m->playable;
            M.page = 0;
            load_pictures(M.cur);
            M.screen = MS_BRIEF;
            return MC_NONE;
        }
        if (a == GA_OK) say("NOT AVAILABLE YET.");
        return MC_NONE;
    case MS_BRIEF:
        if (a == GA_CANCEL) { M.screen = MS_LIST; return MC_NONE; }
        if (a == GA_LEFT || a == GA_UP) { if (M.page > 0) --M.page; else M.snd = -2; return MC_NONE; }
        if (a == GA_RIGHT || a == GA_DOWN || a == GA_OK) {
            if (M.page + 1 < brief_pages()) { ++M.page; return MC_NONE; }
            if (a != GA_OK) { M.snd = -2; return MC_NONE; }
            if (M.view_only) { say("NOT AVAILABLE YET."); return MC_NONE; }
            M.ask = 0;
            M.screen = MS_ASK;
        }
        return MC_NONE;
    case MS_ASK:
        if (a == GA_LEFT || a == GA_RIGHT || a == GA_UP || a == GA_DOWN) { M.ask = 1 - M.ask; return MC_NONE; }
        if (a == GA_CANCEL || (a == GA_OK && M.ask == 1)) { M.screen = MS_BRIEF; return MC_NONE; }
        if (a != GA_OK) return MC_NONE;
        {
            acr_gen1_fit_numbers n;
            const acr_gen1_sortie s = M.h && M.cat ? acr_gen1_sortie_from_fail(acr_gen1_garage_check(M.h, M.cat, &n))
                                                   : ACR_GEN1_SORTIE_INCOMPLETE;
            if (s != ACR_GEN1_SORTIE_OK) { say(acr_gen1_sortie_message(s)); M.screen = MS_BRIEF; return MC_NONE; }
        }
        if (!prepare_sortie()) { say("NOT AVAILABLE YET."); M.screen = MS_BRIEF; return MC_NONE; }
        M.screen = MS_LIST;
        return MC_SORTIE;
    case MS_REPORT:
        if (a != GA_OK && a != GA_CANCEL) return MC_NONE;
        if (M.report_ms < 46 * 1000 / 30) { M.report_ms = 46 * 1000 / 30; M.snd = -3; return MC_NONE; }   /* show it all */
        M.screen = MS_LIST;
        return MC_NONE;
    }
    return MC_NONE;
}

missions_command missions_input(garage_action a) {
    if (a == GA_NONE) return MC_NONE;
    const int screen = M.screen, cur = M.cur, page = M.page, ask = M.ask;
    M.err = 0;
    M.snd = -1;
    const missions_command c = input(a);
    int k = M.snd == -2 ? SFX_UI_BACK : M.snd == -3 ? -1 : M.snd;
    if (M.snd == -1) {
        if (M.err) k = SFX_UI_BACK;
        else if (a == GA_OK) k = SFX_UI_OK;
        else if (a == GA_CANCEL) k = SFX_UI_BACK;
        else if (M.screen != screen || M.cur != cur || M.page != page || M.ask != ask) k = SFX_UI_CURSOR;
    }
    if (k >= 0) sfx_ui(k);
    return c;
}

/* ---- drawing ---- */

static void panel(float x, float y, float w, float h) {
    acr_hud_rect(x, y, w, h, 10, 14, 20, 210, 1);
    acr_hud_rect(x, y, w, h, 70, 90, 100, 255, 0);
}

static void row_bar(float x, float y, float w) { acr_hud_rect(x - 6, y - 3, w, 14, 60, 70, 40, 255, 1); }

static void text_right(float rx, float y, const char *t, uint8_t r, uint8_t g, uint8_t b) {
    acr_hud_text(rx - 8.0f * (float)SDL_strlen(t), y, t, r, g, b);
}

static void header(float hud_w, const char *title) {
    acr_hud_rect(0, 0, hud_w, 480, 8, 10, 14, 255, 1);
    acr_hud_rect(0, 0, hud_w, 36, 18, 24, 32, 255, 1);
    acr_hud_rect(0, 36, hud_w, 1, 70, 90, 100, 255, 1);
    acr_hud_text(24, 14, title, HILITE);
    if (M.h) {
        char c[24], line[48];
        num(M.h->credits, c, sizeof c);
        SDL_snprintf(line, sizeof line, "CREDITS %s C", c);
        text_right(hud_w - 24, 14, line, WHITE);
    }
}

static void message(float hud_w, int elapsed_ms) {
    if (M.message_ms <= 0) return;
    M.message_ms -= elapsed_ms;
    const float w = 8.0f * (float)SDL_strlen(M.message) + 32;
    panel(hud_w * 0.5f - w * 0.5f, 430, w, 22);
    acr_hud_text(hud_w * 0.5f - w * 0.5f + 16, 437, M.message, HILITE);
}

static void hints(const char *t) { acr_hud_text(24, 462, t, DIM); }

static void upper(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (; *in && o + 1 < cap; ++in) out[o++] = (char)(*in >= 'a' && *in <= 'z' ? *in - 32 : *in);
    out[o] = 0;
}

static const char *status_of(const acr_gen1_mission_info *m, uint8_t c[3]) {
    const unsigned f = acr_gen1_mission_flags(&g_record, m->id);
    static const uint8_t good[3] = { GOOD }, dim[3] = { DIM }, white[3] = { WHITE }, bad[3] = { BAD };
    const uint8_t *k = dim;
    const char *t = "NOT AVAILABLE";
    if (m->playable) {
        t = f & ACR_GEN1_MISSION_DONE_BIT ? "COMPLETED" : f & ACR_GEN1_MISSION_FAILED_BIT ? "FAILED" : "OPEN";
        k = f & ACR_GEN1_MISSION_DONE_BIT ? good : f & ACR_GEN1_MISSION_FAILED_BIT ? bad : white;
    }
    memcpy(c, k, 3);
    return t;
}

static void draw_list(float hud_w) {
    header(hud_w, "MISSIONS");
    const float x = 40, w = SDL_max(560.0f, SDL_min(hud_w - 80.0f, 760.0f));
    panel(x - 16, 48, w + 32, 34 + 22.0f * LIST_ROWS);
    acr_hud_text(x, 56, "MISSION", DIM);
    acr_hud_text(x + 236, 56, "REQUESTER", DIM);
    text_right(x + w - 136, 56, "REWARD", DIM);
    text_right(x + w, 56, "STATUS", DIM);
    for (int i = 0; i < LIST_ROWS && M.top + i < M.list.count; ++i) {
        const int row = M.top + i;
        const acr_gen1_mission_info *m = &M.list.m[row];
        const float y = 78 + 22.0f * (float)i;
        const int sel = row == M.cur;
        if (sel) row_bar(x, y, w + 12);
        char name[48], line[48], rq[48];
        SDL_snprintf(name, sizeof name, "%s%s", m->own ? m->id : "", m->own ? "  " : "");
        SDL_strlcat(name, m->name, sizeof name);
        upper(name, line, sizeof line);
        const uint8_t r = m->playable ? (sel ? 255 : 220) : 120, g = m->playable ? (sel ? 220 : 230) : 135,
                      b = m->playable ? (sel ? 120 : 235) : 145;
        acr_hud_text(x, y, line, r, g, b);
        upper(m->requester[0] ? m->requester : "-", rq, sizeof rq);
        rq[w >= 700 ? 26 : 18] = 0;
        acr_hud_text(x + 236, y, rq, m->playable ? 220 : 120, m->playable ? 230 : 135, m->playable ? 235 : 145);
        char c[24];
        num(m->reward, c, sizeof c);
        SDL_snprintf(line, sizeof line, "%s C", c);
        text_right(x + w - 136, y, line, m->playable ? 220 : 120, m->playable ? 230 : 135, m->playable ? 235 : 145);
        uint8_t k[3];
        const char *st = status_of(m, k);
        text_right(x + w, y, st, k[0], k[1], k[2]);
    }
    if (M.list.count > LIST_ROWS) {
        char line[32];
        SDL_snprintf(line, sizeof line, "%d-%d / %d", M.top + 1, SDL_min(M.top + LIST_ROWS, M.list.count), M.list.count);
        text_right(x + w - 136 - 120, 56, line, DIM);
    }
    /* MISSION REPORT, as the game's SYSTEM screen gives it: Failure % is
     * 100 - Success %. */
    const float ry = 48 + 34 + 22.0f * LIST_ROWS + 8;
    panel(x - 16, ry, w + 32, 44);
    const int pct = g_record.sorties ? g_record.successes * 100 / g_record.sorties : 0;
    char line[128];
    SDL_snprintf(line, sizeof line, "MISSION REPORT   SORTIES %u   SUCCESS %u (%d%%)   FAILURE %u (%d%%)",
                 g_record.sorties, g_record.successes, pct, g_record.failures, g_record.sorties ? 100 - pct : 0);
    acr_hud_text(x, ry + 10, line, WHITE);
    if (!M.moa) acr_hud_text(x, ry + 26, "NO MASTER OF ARENA MISSIONS IN THIS IMPORT: RE-IMPORT YOUR DISC.", DIM);
    else acr_hud_text(x, ry + 26, "MASTER OF ARENA'S 20 MISSIONS ARE FROM YOUR DISC.", DIM);
    hints("UP/DOWN: CHOOSE   A: BRIEFING   TAB / TRIANGLE: READ ANY BRIEFING   B: BACK");
}

/* One line of briefing text with the disc's markup: "@1" .. "@0"
 * highlighted, "%n" (picture n) not drawn but added to `pics` (a bit per
 * picture). */
static void markup_line(float x, float y, const char *s, size_t n, int *hi, unsigned *pics) {
    char seg[128];
    size_t k = 0;
    for (size_t i = 0; i <= n; ++i) {
        const int end = i == n;
        const int code = !end && (s[i] == '@' || s[i] == '%') && i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '9';
        if ((end || code) && k) {
            seg[k] = 0;
            if (*hi) acr_hud_text_size(x, y, seg, TEXT_SIZE, HILITE, 255);
            else acr_hud_text_size(x, y, seg, TEXT_SIZE, WHITE, 255);
            x += TEXT_SIZE * (float)k;
            k = 0;
        }
        if (end) break;
        if (code) {
            if (s[i] == '@') *hi = s[i + 1] == '1';
            else *pics |= 1u << (s[i + 1] - '0');
            ++i;
            continue;
        }
        if (k + 1 < sizeof seg) seg[k++] = s[i];
    }
}

static void draw_brief(float hud_w) {
    const acr_gen1_mission_info *m = &M.list.m[M.cur];
    char title[64], nm[48];
    upper(m->name, nm, sizeof nm);
    SDL_snprintf(title, sizeof title, "MISSIONS  -  %s", nm);
    header(hud_w, title);
    const float x = 40, w = SDL_max(560.0f, SDL_min(hud_w - 80.0f, 760.0f)), y0 = 56, ph = 330;
    panel(x - 16, y0, w + 32, ph);
    unsigned pics = 0;
    if (M.page == 0) {
        /* The mail header, laid out as the disc's own header page. */
        char line[96], c[24];
        float y = y0 + 24;
        if (m->sender[0]) {
            SDL_snprintf(line, sizeof line, "Sender:    %s", m->sender);
            acr_hud_text_size(x, y, line, TEXT_SIZE, WHITE, 255);
            y += TEXT_SIZE * 1.6f;
        }
        SDL_snprintf(line, sizeof line, "Requester: %s", m->requester[0] ? m->requester : "-");
        acr_hud_text_size(x, y, line, TEXT_SIZE, WHITE, 255);
        y += TEXT_SIZE * 3.2f;
        num(m->reward, c, sizeof c);
        SDL_snprintf(line, sizeof line, "Reward:    %s C", c);
        acr_hud_text_size(x, y, line, TEXT_SIZE, HILITE, 255);
        y += TEXT_SIZE * 3.2f;
        if (m->own)
            acr_hud_text(x, y, "ARMORED-CORER MISSION (PROLOGUE 4, STANDARD VARIANT).", DIM);
        else {
            acr_hud_text(x, y, "FROM YOUR MASTER OF ARENA DISC.", DIM);
            if (!m->pages) acr_hud_text(x, y + 16, "THE DISC HAS NO BRIEFING FOR THIS MISSION.", DIM);
        }
    } else {
        const char *t = m->page[M.page - 1];
        float y = y0 + 24;
        int hi = 0;
        while (*t) {
            const char *nl = SDL_strchr(t, '\n');
            const size_t n = nl ? (size_t)(nl - t) : SDL_strlen(t);
            markup_line(x, y, t, n, &hi, &pics);
            y += TEXT_SIZE * 1.6f;
            t += n + (nl ? 1 : 0);
        }
    }
    /* The pictures the page names, beside the text (104 x 100 on the PS1). */
    {
        const float pw = 146, pht = 140;
        float py = y0 + 12;
        for (int k = 0; k < ACR_GEN1_MISSION_PAGES; ++k) {
            if (!(pics & (1u << k)) || !M.pic[k].id || py + pht > y0 + ph - 24) continue;
            const float px = x + w - pw;
            acr_hud_rect(px - 2, py - 2, pw + 4, pht + 4, 70, 90, 100, 255, 0);
            acr_hud_sprite(M.pic[k].id, M.pic[k].w, M.pic[k].h, 0, 0, (float)M.pic[k].w, (float)M.pic[k].h,
                           px, py, pw, pht, 255, 255, 255, 255, ACR_HUD_ALPHA);
            py += pht + 10;
        }
    }
    char line[64];
    SDL_snprintf(line, sizeof line, "PAGE %d / %d", M.page + 1, brief_pages());
    text_right(x + w, y0 + ph - 18, line, DIM);
    if (M.view_only) acr_hud_text(x, y0 + ph - 18, "NOT AVAILABLE YET: READ ONLY", BAD);
    if (M.screen == MS_ASK) {
        const float aw = 300, ax = hud_w * 0.5f - aw * 0.5f, ay = 372;
        panel(ax, ay, aw, 48);
        acr_hud_text(ax + 16, ay + 10, "ACCEPT THIS MISSION?", WHITE);
        acr_hud_text(ax + 60, ay + 28, "YES", M.ask == 0 ? 255 : 120, M.ask == 0 ? 220 : 135, M.ask == 0 ? 120 : 145);
        acr_hud_text(ax + 200, ay + 28, "NO", M.ask == 1 ? 255 : 120, M.ask == 1 ? 220 : 135, M.ask == 1 ? 120 : 145);
    }
    hints(M.page + 1 < brief_pages() ? "LEFT/RIGHT: PAGE   A: NEXT PAGE   B: BACK"
                                     : M.view_only ? "LEFT: PAGE   B: BACK" : "LEFT: PAGE   A: ACCEPT   B: BACK");
}

/* A report number as the game draws it (0x8009159C): 8-pixel digits from
 * the "0123456789-" strip, right-aligned in ten places from x, red when
 * below zero. */
static void report_number(float ox, float s, int x, int y, int32_t v) {
    const tex *t = v < 0 ? &M.digits_red : &M.digits;
    if (!t->id) return;
    char d[16];
    int n = 0;
    long u = v;
    if (u < 0) { d[n++] = 10; u = -u; }
    char tmp[12];
    int k = 0;
    do { tmp[k++] = (char)(u % 10); u /= 10; } while (u && k < 10);
    while (k) d[n++] = tmp[--k];
    float px = (float)(x + (9 - n) * 8);
    for (int i = 0; i < n; ++i, px += 8)
        acr_hud_sprite(t->id, t->w, t->h, 8.0f * (float)d[i], 0, 8, 16, ox + px * s, (float)y * s, 8 * s, 16 * s,
                       255, 255, 255, 255, ACR_HUD_ALPHA);
}

static void draw_report(float hud_w, int elapsed_ms) {
    acr_hud_rect(0, 0, hud_w, 480, 0, 0, 0, 255, 1);
    const tex *p = &M.report[M.report_pic];
    const float s = 2.0f, ox = (hud_w - 320.0f * s) * 0.5f;
    M.report_ms += elapsed_ms;
    if (!p->id) {
        acr_hud_text(40, 60, "RE-IMPORT YOUR DISC FOR THE INCOME AND EXPENSE REPORT.", WHITE);
    } else {
        acr_hud_sprite(p->id, p->w, p->h, 0, 0, (float)p->w, (float)p->h, ox, 0, 320 * s, 240 * s, 255, 255, 255, 255,
                       ACR_HUD_ALPHA);
    }
    /* Rows as 0x80091804 places them, shown one after another (frames 0,
     * 5, 10, 15, 20, 30, 45 of the screen). */
    static const struct { int y, frame; } k_row[7] = {
        { 0x38, 0 }, { 0x48, 5 }, { 0x5E, 10 }, { 0x6F, 15 }, { 0x80, 20 }, { 0x9D, 30 }, { 0xBD, 45 } };
    const int32_t v[7] = { M.rep.reward, M.rep.addition, M.rep.ammo, M.rep.repair, M.rep.deduction, M.rep.balance,
                           M.rep.credits };
    const int frame = M.report_ms * 30 / 1000;
    for (int i = 0; i < 7; ++i)
        if (frame >= k_row[i].frame) report_number(ox, s, 0xCA, k_row[i].y, v[i]);
    (void)hud_w;
}

void missions_draw(float hud_w, int elapsed_ms) {
    switch (M.screen) {
    case MS_LIST: draw_list(hud_w); break;
    case MS_BRIEF: case MS_ASK: draw_brief(hud_w); break;
    case MS_REPORT: draw_report(hud_w, elapsed_ms); break;
    }
    if (M.screen == MS_REPORT && M.report_ms >= 46 * 1000 / 30)
        hints(M.rep.success ? "MISSION ACCOMPLISHED   A: CONTINUE" : "MISSION FAILED   A: CONTINUE");
    message(hud_w, elapsed_ms);
}
