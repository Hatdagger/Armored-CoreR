#include "arena.h"

#include <SDL3/SDL.h>

#include <string.h>

#include "../../gen1/acr_gen1_arena.h"
#include "../../render/acr_render.h"
#include "sfx.h"
#include "vs_screen.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define GOOD 120, 230, 140
#define BAD 255, 90, 70

enum { AS_PICK, AS_LADDER, AS_ASK, AS_VS, AS_RESULT };

/* The stages offered (the four that matter for the current build, user
 * 2026-11-19; as Rogue Arena). Green Silo is the default. */
static const struct { int id; const char *name; } k_stage[] = {
    { 27, "GREEN SILO" }, { 39, "OCEAN PLATFORM" }, { 25, "GARDEN PLAZA" }, { 36, "NIGHT CITY" },
};
#define NSTAGE ((int)(sizeof k_stage / sizeof k_stage[0]))

#define LADDER_ROWS 16

static struct {
    int ok;
    char import_dir[1024];
    acr_gen1_rogue_foe roster[ACR_GEN1_ROGUE_ROSTER];
    acr_gen1_arena_world w;
    acr_gen1_arena s;
    acr_gen1_hangar *h;
    const acr_gen1_catalog *cat;
    char (*names)[ACR_GEN1_PART_MAX][24];
    int screen, cur;                     /* AS_PICK: the row */
    int arena, page, lcur, ltop;         /* AS_LADDER: arena, page (arena 0: 0 ranked, 1 sub), row, scroll */
    int ask;                             /* AS_ASK: 0 YES, 1 NO */
    int fight_arena, fight_rank;
    rogue_fight fight;
    acr_gen1_arena_payout pay;
    char pay_foe[48];
    char message[96];
    int message_ms, err, snd;
    int stages[NSTAGE], nstages;
    float spin;
    char vs_right[40];                   /* AS_VS: the top right line */
} A;

static rogue_ac_drawer g_draw_ac;
void arena_set_ac_drawer(rogue_ac_drawer fn) { g_draw_ac = fn; vs_set_ac_drawer(fn); }

static void say(const char *t) {
    SDL_strlcpy(A.message, t, sizeof A.message);
    A.message_ms = 2500;
    A.err = 1;
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

/* ---- files ---- */

static void file_path(char *out, size_t size) {
    const char *env = SDL_getenv("ACR_ARENA_FILE");
    if (env && env[0]) { SDL_strlcpy(out, env, size); return; }
    char *pref = SDL_GetPrefPath("Hatdagger", "Armored-CoreR");
    SDL_snprintf(out, size, "%sarena.toml", pref ? pref : "");
    SDL_free(pref);
}

static void save(void) {
    static char text[32768];
    char path[1200];
    const size_t n = acr_gen1_arena_write(&A.s, text, sizeof text);
    file_path(path, sizeof path);
    if (n < sizeof text) SDL_SaveFile(path, text, n);
}

static void load(void) {
    char path[1200];
    file_path(path, sizeof path);
    char *t = SDL_LoadFile(path, NULL);
    acr_gen1_arena s;
    if (t && acr_gen1_arena_read(&s, t)) {
        A.s = s;
        if (acr_gen1_arena_sync(&A.s, &A.w)) save();
    } else {
        acr_gen1_arena_new(&A.s, &A.w);
    }
    SDL_free(t);
}

/* ---- setup ---- */

static int stage_exists(int id) {
    char path[1300];
    SDL_snprintf(path, sizeof path, "%s/stages/%d", A.import_dir, id);
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static int stage_now(void) {
    for (int i = 0; i < A.nstages; ++i) if (k_stage[A.stages[i]].id == A.s.stage) return A.s.stage;
    return A.nstages ? k_stage[A.stages[0]].id : 27;
}

static const char *stage_name(int id) {
    for (int i = 0; i < NSTAGE; ++i) if (k_stage[i].id == id) return k_stage[i].name;
    return "STAGE";
}

int arena_init(const char *import_dir, acr_gen1_hangar *hangar, const acr_gen1_catalog *cat,
               char names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24]) {
    SDL_strlcpy(A.import_dir, import_dir, sizeof A.import_dir);
    A.h = hangar;
    A.cat = cat;
    A.names = names;
    SDL_zero(A.w);
    A.w.foe = A.roster;
    A.w.foes = demo_rogue_roster(import_dir, A.roster, ACR_GEN1_ROGUE_ROSTER, names);
    A.w.cat = cat;
    /* Leg kinds for the Ex Arenas: caterpillars from 27, four legs 21-26
     * (as the fight sorts them), reverse joints by name (LB-), the rest
     * humanoid. */
    for (int i = 0; i < ACR_GEN1_PART_MAX; ++i) {
        const char *n = names ? names[ACR_GEN1_CAT_LEGS][i] : "";
        A.w.legs_kind[i] = (uint8_t)(i >= 27 ? ACR_GEN1_ARENA_LEGS_CATERPILLAR
                                   : i >= 21 ? ACR_GEN1_ARENA_LEGS_FOUR
                                   : (n[0] == 'L' && n[1] == 'B') ? ACR_GEN1_ARENA_LEGS_REVERSE
                                   : ACR_GEN1_ARENA_LEGS_HUMANOID);
    }
    A.nstages = 0;
    for (int i = 0; i < NSTAGE; ++i) if (stage_exists(k_stage[i].id)) A.stages[A.nstages++] = i;
    load();
    A.ok = A.s.a[0].places > 0;
    A.screen = AS_PICK;
    return A.ok;
}

void arena_open(void) {
    A.screen = AS_PICK;
    A.cur = 0;
    A.message[0] = 0;
    A.message_ms = 0;
}

/* ---- the ladder ---- */

static int legs_kind(void) {
    const int l = A.h ? A.h->fitted[ACR_GEN1_SLOT_LEGS] : ACR_GEN1_NO_PART;
    return l < ACR_GEN1_PART_MAX ? A.w.legs_kind[l] : 0;
}

static unsigned build_fail(void) {
    if (!A.h || !A.cat) return 1;
    acr_gen1_fit_numbers n;
    return acr_gen1_garage_check(A.h, A.cat, &n);
}

static acr_gen1_arena_why why_at(int rank) {
    return acr_gen1_arena_can_challenge(&A.s, A.arena, rank, legs_kind(), build_fail());
}

/* The rows of the ladder page: first rank and count. */
static void page_range(int *first, int *count) {
    const int places = A.s.a[A.arena].places;
    if (A.arena == 0 && places > ACR_GEN1_ARENA_RANKED) {
        if (A.page == 0) { *first = 1; *count = ACR_GEN1_ARENA_RANKED; }
        else { *first = ACR_GEN1_ARENA_RANKED + 1; *count = places - ACR_GEN1_ARENA_RANKED; }
    } else {
        *first = 1; *count = places;
    }
}

static int cursor_rank(void) {
    int first, count;
    page_range(&first, &count);
    return first + A.lcur;
}

static void scroll_to_cursor(void) {
    if (A.lcur < A.ltop) A.ltop = A.lcur;
    if (A.lcur >= A.ltop + LADDER_ROWS) A.ltop = A.lcur - LADDER_ROWS + 1;
    if (A.ltop < 0) A.ltop = 0;
}

/* The page with the player on it, the cursor on the nearest opponent
 * that can be challenged above (or the player's row). */
static void ladder_home(void) {
    const int me = acr_gen1_arena_rank(&A.s, A.arena);
    A.page = acr_gen1_arena_is_sub(A.arena, me) ? 1 : 0;
    /* At the top of the sub arena the ranked list is the way on. */
    if (A.arena == 0 && me == ACR_GEN1_ARENA_RANKED + 1) A.page = 0;
    int first, count;
    page_range(&first, &count);
    int target = me;
    for (int r = me - 1; r >= first; --r)
        if (why_at(r) == ACR_GEN1_ARENA_OK) { target = r; break; }
    if (target < first || target >= first + count) target = me >= first && me < first + count ? me : first + count - 1;
    A.lcur = target - first;
    A.ltop = 0;
    scroll_to_cursor();
}

static void foe_name(const acr_gen1_rogue_foe *f, char *out, size_t size) {
    if (!f) { SDL_snprintf(out, size, "?"); return; }
    if (f->ac[0] && f->pilot[0]) SDL_snprintf(out, size, "%s / %s", f->ac, f->pilot);
    else SDL_snprintf(out, size, "%s", f->ac[0] ? f->ac : f->pilot);
}

static const acr_gen1_rogue_foe *foe_at_in(int arena, int rank) {
    const int d = acr_gen1_arena_holder(&A.s, arena, rank);
    return d ? acr_gen1_arena_foe(&A.w, arena, d) : NULL;
}

static const acr_gen1_rogue_foe *foe_at(int rank) { return foe_at_in(A.arena, rank); }

static const char *part_name(int cat, int index) {
    if (!A.names || cat < 0 || cat >= ACR_GEN1_CAT_COUNT || index < 0 || index >= ACR_GEN1_PART_MAX) return "A PART";
    return A.names[cat][index][0] ? A.names[cat][index] : "A PART";
}

static int prepare_fight(int rank) {
    const acr_gen1_rogue_foe *f = foe_at(rank);
    if (!f || !A.h) return 0;
    rogue_fight *g = &A.fight;
    SDL_zero(*g);
    for (int s = 0; s < SLOT_COUNT; ++s) {
        g->parts[0][s] = A.h->fitted[s];
        g->parts[1][s] = f->parts[s];
    }
    memcpy(g->colours[0], A.h->colours, sizeof g->colours[0]);
    memcpy(g->colours[1], f->colours, sizeof g->colours[1]);
    g->plus[0] = 0;
    g->plus[1] = f->ai.rebel;
    g->ai = f->ai;
    g->stage = stage_now();
    SDL_snprintf(g->name[0], sizeof g->name[0], "%s / %s", A.h->ac_name, A.h->pilot_name);
    foe_name(f, g->name[1], sizeof g->name[1]);
    A.fight_arena = A.arena;
    A.fight_rank = rank;
    return 1;
}

/* ---- the VS screen (vs_screen.c, as Rogue Arena's) ----
 * The bottom band is the arena's ladder (user, 2026-11-25): a row per
 * rank, top to bottom in columns of ten, the player's row where it stands
 * (blue), the opponent's (red), the beaten ("DEFEATED") struck through. */

#define VS_LADDER_ROWS 10

static void vs_ladder(float hud_w, float sx, float t, const uint8_t band[3]) {
    (void)sx; (void)band;
    const acr_gen1_arena_ladder *l = &A.s.a[A.fight_arena];
    const int places = l->places, me = acr_gen1_arena_rank(&A.s, A.fight_arena);
    if (places <= 0) return;
    const int cols = (places + VS_LADDER_ROWS - 1) / VS_LADDER_ROWS;
    float colw = (hud_w - 40) / (float)cols;
    if (colw > 200) colw = 200;
    const float x0 = hud_w * 0.5f - colw * (float)cols * 0.5f, y0 = 334, rh = 12;
    const float pulse = 0.5f + 0.5f * SDL_sinf(t * 6.0f);
    const int chars = (int)((colw - 30 - 8) / 8), chars_you = (int)((colw - 30 - 40) / 8);   /* the YOU tag */
    for (int r = 1; r <= places; ++r) {
        const float x = x0 + colw * (float)((r - 1) / VS_LADDER_ROWS), y = y0 + rh * (float)((r - 1) % VS_LADDER_ROWS);
        const int you = r == me, foe = r == A.fight_rank;
        const int down = !you && acr_gen1_arena_defeated(&A.s, A.fight_arena, r);
        if (you) acr_hud_rect(x + 2, y - 2, colw - 6, rh, 40, 64, 104, 255, 1);
        if (foe) {
            const float k = 0.55f + 0.45f * pulse;
            acr_hud_rect(x + 2, y - 2, colw - 6, rh, (uint8_t)(110 * k), (uint8_t)(34 * k), (uint8_t)(30 * k), 255, 1);
            acr_hud_rect(x + 2, y - 2, colw - 6, rh, vs_red[0], vs_red[1], vs_red[2], 255, 0);
        }
        char num_s[8], name[40];
        SDL_snprintf(num_s, sizeof num_s, "%2d", r);
        const int ranked = A.fight_arena == 0 && r <= ACR_GEN1_ARENA_RANKED;
        if (ranked) acr_hud_text(x + 6, y, num_s, 255, 196, 72);
        else acr_hud_text(x + 6, y, num_s, 150, 156, 170);
        const acr_gen1_rogue_foe *f = you ? NULL : foe_at_in(A.fight_arena, r);
        const char *n = you ? (A.h && A.h->pilot_name[0] ? A.h->pilot_name : "YOU") : f ? (f->pilot[0] ? f->pilot : f->ac) : "?";
        const int c = you ? chars_you : chars;
        SDL_snprintf(name, sizeof name, "%.*s", c > 0 ? c : 1, n);
        const float nx = x + 30;
        if (you) {
            acr_hud_text(nx, y, name, 200, 225, 255);
            acr_hud_text(x + colw - 36, y, "YOU", vs_blue[0], vs_blue[1], vs_blue[2]);
        } else if (foe) acr_hud_text(nx, y, name, 255, 236, 230);
        else if (down) {
            acr_hud_text(nx, y, name, 110, 112, 120);
            acr_hud_rect(nx - 1, y + 3, 8.0f * (float)SDL_strlen(name) + 2, 2, 200, 50, 44, 255, 1);
        } else acr_hud_text(nx, y, name, 220, 226, 232);
        /* arena 0: the line between the ranked list and the sub arena */
        if (A.fight_arena == 0 && r == ACR_GEN1_ARENA_RANKED && places > ACR_GEN1_ARENA_RANKED)
            acr_hud_rect(x + 2, y + rh - 2, colw - 6, 1, 255, 196, 72, 255, 1);
    }
}

static void vs_start(void) {
    const int a = A.fight_arena, rank = A.fight_rank, me = acr_gen1_arena_rank(&A.s, a);
    const int rematch = acr_gen1_arena_is_rematch(&A.s, a, rank);
    if (a == 0 && A.s.a[0].places > ACR_GEN1_ARENA_RANKED)
        SDL_snprintf(A.vs_right, sizeof A.vs_right, "%s", acr_gen1_arena_is_sub(a, rank) ? "SUB ARENA" : "RANKED");
    else SDL_snprintf(A.vs_right, sizeof A.vs_right, "%s", acr_gen1_arena_name(a));
    vs_setup s;
    SDL_zero(s);
    s.fight = &A.fight;
    s.mode = a == 0 ? "ARENA" : "EX ARENA";
    s.right = A.vs_right;
    s.band[0] = 0xEC; s.band[1] = 0xA8; s.band[2] = 0x24;
    s.rank[0] = me;
    s.rank[1] = rank;
    /* The player's emblem: none yet (the garage has no EDIT EMBLEM). */
    s.emblem[1] = vs_emblem(A.import_dir, a, acr_gen1_arena_holder(&A.s, a, rank));
    s.for_rank = rematch ? 0 : rank;
    s.tag = rematch ? "REMATCH" : NULL;
    s.stage = stage_name(A.fight.stage);
    s.hint = "ENTER / A  SORTIE NOW    ESC / B  BACK TO THE LADDER";
    s.strip = vs_ladder;
    vs_open(&s);
}

int arena_vs_due(void) { return A.screen == AS_VS && vs_due(); }

const rogue_fight *arena_fight_info(void) { return &A.fight; }
int32_t arena_round_ms(void) { return 0; }   /* no clock in the Arena */

int arena_fight_over(int result) {
    const acr_gen1_rogue_foe *f = acr_gen1_arena_foe(&A.w, A.fight_arena,
                                                     acr_gen1_arena_holder(&A.s, A.fight_arena, A.fight_rank));
    foe_name(f, A.pay_foe, sizeof A.pay_foe);
    SDL_zero(A.pay);
    A.pay.prize_id = A.pay.part_cat = A.pay.part_index = -1;
    const int booked = acr_gen1_arena_result(&A.s, &A.w, A.fight_arena, A.fight_rank, result == 1, &A.pay);
    if (booked && A.pay.won && A.h) acr_gen1_arena_pay(A.h, &A.pay);
    save();
    A.arena = A.fight_arena;
    A.screen = AS_RESULT;
    return booked && A.pay.won;
}

/* ---- input ---- */

static int step(int cur, int n, garage_action a) {
    if (n <= 0) return 0;
    if (a == GA_UP) return (cur + n - 1) % n;
    if (a == GA_DOWN) return (cur + 1) % n;
    return cur;
}

/* No match timer: an Arena fight goes on until one side is down or out of
 * the area, or the player gives up (a loss) (user, 2026-11-25). */
enum { PR_STAGE = ACR_GEN1_ARENA_COUNT, PR_BACK, PR_COUNT };

static void cycle_stage(int dir) {
    if (!A.nstages) { say("RE-IMPORT YOUR DISC: NO STAGES FOUND."); return; }
    int i = 0;
    for (int k = 0; k < A.nstages; ++k) if (k_stage[A.stages[k]].id == stage_now()) i = k;
    i = (i + dir + A.nstages) % A.nstages;
    A.s.stage = (uint8_t)k_stage[A.stages[i]].id;
    save();
}

static arena_command input(garage_action a) {
    switch (A.screen) {
    case AS_PICK:
        if (!A.ok) { if (a == GA_OK || a == GA_CANCEL) return AC_LEAVE; return AC_NONE; }
        A.cur = step(A.cur, PR_COUNT, a);
        if (a == GA_CANCEL) return AC_LEAVE;
        if (A.cur == PR_STAGE && (a == GA_LEFT || a == GA_RIGHT || a == GA_OK)) {
            cycle_stage(a == GA_LEFT ? -1 : 1);
            return AC_NONE;
        }
        if (a != GA_OK) return AC_NONE;
        if (A.cur == PR_BACK) return AC_LEAVE;
        {
            const acr_gen1_arena_why why = acr_gen1_arena_open(&A.s, A.cur);
            if (why != ACR_GEN1_ARENA_OK && !SDL_getenv("ACR_ARENA_UNLOCK")) { say(acr_gen1_arena_why_text(why)); return AC_NONE; }
            if (!A.s.a[A.cur].places) { say(acr_gen1_arena_why_text(ACR_GEN1_ARENA_NO_DATA)); return AC_NONE; }
            A.arena = A.cur;
            ladder_home();
            A.screen = AS_LADDER;
        }
        return AC_NONE;
    case AS_LADDER: {
        int first, count;
        page_range(&first, &count);
        if (a == GA_PREV_TAB || a == GA_NEXT_TAB) {
            if (A.arena == 0 && A.s.a[0].places > ACR_GEN1_ARENA_RANKED) {
                A.page = 1 - A.page;
                A.lcur = A.ltop = 0;
            } else A.snd = -2;
            return AC_NONE;
        }
        if (a == GA_UP || a == GA_DOWN) { A.lcur = step(A.lcur, count, a); scroll_to_cursor(); return AC_NONE; }
        if (a == GA_LEFT || a == GA_RIGHT) {   /* a page of rows at a time */
            A.lcur += a == GA_LEFT ? -LADDER_ROWS : LADDER_ROWS;
            if (A.lcur < 0) A.lcur = 0;
            if (A.lcur >= count) A.lcur = count - 1;
            scroll_to_cursor();
            return AC_NONE;
        }
        if (a == GA_CANCEL) { A.screen = AS_PICK; A.cur = A.arena; return AC_NONE; }
        if (a != GA_OK) return AC_NONE;
        const int rank = cursor_rank();
        const acr_gen1_arena_why why = why_at(rank);
        if (why != ACR_GEN1_ARENA_OK) {
            if (rank == acr_gen1_arena_rank(&A.s, A.arena)) say("THAT'S YOU.");
            else say(acr_gen1_arena_why_text(why));
            return AC_NONE;
        }
        if (!A.nstages) { say("RE-IMPORT YOUR DISC: NO STAGES FOUND."); return AC_NONE; }
        A.ask = 0;
        A.screen = AS_ASK;
        return AC_NONE;
    }
    case AS_ASK:
        if (a == GA_LEFT || a == GA_RIGHT || a == GA_UP || a == GA_DOWN) { A.ask = 1 - A.ask; return AC_NONE; }
        if (a == GA_CANCEL || (a == GA_OK && A.ask == 1)) { A.screen = AS_LADDER; return AC_NONE; }
        if (a != GA_OK) return AC_NONE;
        if (!prepare_fight(cursor_rank())) { say("THE OPPONENT COULD NOT BE LOADED."); A.screen = AS_LADDER; return AC_NONE; }
        vs_start();                     /* the fight starts when it's done (arena_vs_due) */
        A.screen = AS_VS;
        return AC_NONE;
    case AS_VS:
        if (vs_closing()) { A.snd = -3; return AC_NONE; }             /* closing: nothing to do (silent) */
        if (a == GA_CANCEL) { A.screen = AS_LADDER; return AC_NONE; }
        if (a == GA_OK) vs_skip();                                    /* skip the count: the shutters now */
        else A.snd = -3;
        return AC_NONE;
    case AS_RESULT:
        if (a != GA_OK && a != GA_CANCEL) return AC_NONE;
        ladder_home();
        A.screen = AS_LADDER;
        return AC_NONE;
    }
    return AC_NONE;
}

arena_command arena_input(garage_action a) {
    if (a == GA_NONE) return AC_NONE;
    const int screen = A.screen, cur = A.cur, lcur = A.lcur, page = A.page, ask = A.ask;
    A.err = 0;
    A.snd = -1;
    const arena_command c = input(a);
    int k = A.snd == -2 ? SFX_UI_BACK : A.snd;   /* -3: silent */
    if (k == -1) {
        if (A.err) k = SFX_UI_BACK;
        else if (a == GA_OK) k = SFX_UI_OK;
        else if (a == GA_CANCEL) k = SFX_UI_BACK;
        else if (A.screen != screen || A.cur != cur || A.lcur != lcur || A.page != page || A.ask != ask) k = SFX_UI_CURSOR;
        else if (a == GA_LEFT || a == GA_RIGHT) k = SFX_UI_CURSOR;   /* stage */
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
    if (A.h) {
        char c[24], line[48];
        num(A.h->credits, c, sizeof c);
        SDL_snprintf(line, sizeof line, "CREDITS %s C", c);
        text_right(hud_w - 24, 14, line, WHITE);
    }
}

static void message(float hud_w, int elapsed_ms) {
    if (A.message_ms <= 0) return;
    A.message_ms -= elapsed_ms;
    const float w = 8.0f * (float)SDL_strlen(A.message) + 32;
    panel(hud_w * 0.5f - w * 0.5f, 430, w, 22);
    acr_hud_text(hud_w * 0.5f - w * 0.5f + 16, 437, A.message, HILITE);
}

static void hints(float hud_w, const char *t) { acr_hud_text(24, 462, t, DIM); (void)hud_w; }

static void draw_pick(float hud_w) {
    header(hud_w, "ARENA");
    if (!A.ok) {
        panel(24, 60, hud_w - 48, 60);
        acr_hud_text(40, 76, "NO ARENA OPPONENTS IN THIS IMPORT.", WHITE);
        acr_hud_text(40, 92, "RE-IMPORT YOUR DISC TO FILL THE ARENA.", DIM);
        hints(hud_w, "A / ENTER: BACK");
        return;
    }
    const float x = 40, w = 520;
    panel(x - 16, 56, w + 32, 22 * PR_COUNT + 40);
    acr_hud_text(x, 64, "ARENA", DIM);
    text_right(x + w - 120, 64, "RANK", DIM);
    text_right(x + w, 64, "STATUS", DIM);
    for (int i = 0; i < PR_COUNT; ++i) {
        const float y = 86 + 22.0f * (float)i + (i >= PR_STAGE ? 10 : 0);
        const int sel = A.cur == i;
        if (sel) row_bar(x, y, w + 12);
        char line[64];
        if (i < ACR_GEN1_ARENA_COUNT) {
            const acr_gen1_arena_why why = acr_gen1_arena_open(&A.s, i);
            const int open = why == ACR_GEN1_ARENA_OK;
            const char *name = acr_gen1_arena_name(i);
            if (i == 0) acr_hud_text(x, y, name, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
            else {
                SDL_snprintf(line, sizeof line, "EX ARENA  %s", name);
                if (open) acr_hud_text(x, y, line, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
                else acr_hud_text(x, y, line, DIM);
            }
            if (A.s.a[i].places) {
                const int me = acr_gen1_arena_rank(&A.s, i);
                SDL_snprintf(line, sizeof line, "%d / %d", me, A.s.a[i].places);
                text_right(x + w - 120, y, line, open ? 220 : 120, open ? 230 : 135, open ? 235 : 145);
            }
            const char *st = why == ACR_GEN1_ARENA_NO_DATA ? "NO DATA" : !open ? "LOCKED"
                           : acr_gen1_arena_rank(&A.s, i) == 1 ? "CHAMPION" : "OPEN";
            if (open) text_right(x + w, y, st, GOOD); else text_right(x + w, y, st, DIM);
        } else if (i == PR_STAGE) {
            acr_hud_text(x, y, "STAGE", sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
            SDL_snprintf(line, sizeof line, "< %s >", A.nstages ? stage_name(stage_now()) : "NONE");
            text_right(x + w, y, line, WHITE);
        } else {
            acr_hud_text(x, y, "BACK", sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
        }
    }
    /* ARENA REPORT for the row under the cursor. */
    if (A.cur < ACR_GEN1_ARENA_COUNT && A.s.a[A.cur].places) {
        const acr_gen1_arena_ladder *l = &A.s.a[A.cur];
        const float ry = 86 + 22.0f * PR_COUNT + 40;
        panel(x - 16, ry, w + 32, 50);
        char line[96];
        const int pct = l->challenges ? l->wins * 100 / l->challenges : 0;
        SDL_snprintf(line, sizeof line, "CHALLENGE %u   WIN %u (%d%%)   LOSE %u (%d%%)", l->challenges, l->wins, pct,
                     l->losses, l->challenges ? 100 - pct : 0);
        acr_hud_text(x, ry + 10, line, WHITE);
        int beaten = 0;
        for (int r = 1; r < l->places; ++r) beaten += l->beaten[r - 1] != 0;
        SDL_snprintf(line, sizeof line, "OPPONENTS BEATEN %d / %d   BEST RANK %d", beaten, l->places - 1,
                     l->best ? l->best : acr_gen1_arena_rank(&A.s, A.cur));
        acr_hud_text(x, ry + 26, line, DIM);
    }
    hints(hud_w, "UP/DOWN: CHOOSE   LEFT/RIGHT: STAGE   A: ENTER   B: BACK");
}

static void status_of(int rank, const char **text, uint8_t c[3]) {
    const int me = acr_gen1_arena_rank(&A.s, A.arena);
    static const uint8_t good[3] = { GOOD }, dim[3] = { DIM }, hi[3] = { HILITE }, bad[3] = { BAD };
    const uint8_t *k = dim;
    if (rank == me) { *text = "YOU"; k = hi; }
    else if (acr_gen1_arena_defeated(&A.s, A.arena, rank) && rank > me) {
        static const uint8_t down[3] = { 200, 110, 100 };
        *text = "DEFEATED";             /* a rematch, for fun: no rank, no prize */
        k = why_at(rank) == ACR_GEN1_ARENA_OK ? down : dim;
    } else {
        switch (why_at(rank)) {
        case ACR_GEN1_ARENA_OK: *text = "CHALLENGE"; k = good; break;
        case ACR_GEN1_ARENA_DONE: *text = "BEATEN"; break;
        case ACR_GEN1_ARENA_WRONG_LEGS: *text = "LEG TYPE"; k = bad; break;
        case ACR_GEN1_ARENA_INCOMPLETE: *text = "CHALLENGE"; k = bad; break;
        default: *text = "REFERENCE ONLY"; break;
        }
    }
    memcpy(c, k, 3);
}

static void draw_ladder(float hud_w, int elapsed_ms) {
    char title[64];
    const int me = acr_gen1_arena_rank(&A.s, A.arena);
    if (A.arena == 0 && A.s.a[0].places > ACR_GEN1_ARENA_RANKED)
        SDL_snprintf(title, sizeof title, "ARENA  -  %s", A.page ? "SUB ARENA" : "RANKED");
    else SDL_snprintf(title, sizeof title, "EX ARENA  -  %s", acr_gen1_arena_name(A.arena));
    header(hud_w, title);
    int first, count;
    page_range(&first, &count);
    const float lx = 36, lw = 380;
    panel(lx - 16, 48, lw + 28, 26 + 22.0f * LADDER_ROWS);
    acr_hud_text(lx, 56, "RANK", DIM);
    acr_hud_text(lx + 52, 56, "PILOT", DIM);
    text_right(lx + lw, 56, "CHALLENGE", DIM);
    for (int i = 0; i < LADDER_ROWS && A.ltop + i < count; ++i) {
        const int rank = first + A.ltop + i;
        const float y = 76 + 22.0f * (float)i;
        const int sel = A.ltop + i == A.lcur;
        if (sel) row_bar(lx, y, lw + 12);
        char line[64];
        SDL_snprintf(line, sizeof line, "%2d", rank);
        acr_hud_text(lx + 4, y, line, WHITE);
        const acr_gen1_rogue_foe *f = foe_at(rank);
        if (rank == me && A.h) SDL_snprintf(line, sizeof line, "%.20s", A.h->pilot_name[0] ? A.h->pilot_name : "YOU");
        else SDL_snprintf(line, sizeof line, "%.20s", f ? (f->pilot[0] ? f->pilot : f->ac) : "?");
        if (rank == me) acr_hud_text(lx + 52, y, line, HILITE);
        else if (sel) acr_hud_text(lx + 52, y, line, HILITE);
        else acr_hud_text(lx + 52, y, line, WHITE);
        const char *st;
        uint8_t c[3];
        status_of(rank, &st, c);
        text_right(lx + lw, y, st, c[0], c[1], c[2]);
    }
    if (count > LADDER_ROWS) {   /* scroll marks */
        if (A.ltop > 0) acr_hud_text(lx + lw / 2, 64, "^", DIM);
        if (A.ltop + LADDER_ROWS < count) acr_hud_text(lx + lw / 2, 76 + 22.0f * LADDER_ROWS - 6, "v", DIM);
    }
    /* The opponent under the cursor. */
    const float px = lx + lw + 28, pw = hud_w - px - 20;
    const int rank = first + A.lcur;
    const acr_gen1_rogue_foe *f = foe_at(rank);
    panel(px, 48, pw, 382);
    char line[96];
    if (rank == me && A.h) {
        acr_hud_text(px + 12, 58, A.h->ac_name, HILITE);
        acr_hud_text(px + 12, 72, A.h->pilot_name, WHITE);
        if (g_draw_ac) {
            int parts[SLOT_COUNT];
            for (int s = 0; s < SLOT_COUNT; ++s) parts[s] = A.h->fitted[s];
            g_draw_ac(parts, (const uint8_t (*)[3])A.h->colours, px + 8, 92, pw - 16, 230, 1.0f, A.spin, 0, 1.0f);
        }
        SDL_snprintf(line, sizeof line, "YOUR RANK  %d / %d", me, A.s.a[A.arena].places);
        acr_hud_text(px + 12, 336, line, WHITE);
    } else if (f) {
        acr_hud_text(px + 12, 58, f->ac[0] ? f->ac : "AC", HILITE);
        acr_hud_text(px + 12, 72, f->pilot, WHITE);
        /* its emblem behind the AC */
        vs_emblem_draw(vs_emblem(A.import_dir, A.arena, acr_gen1_arena_holder(&A.s, A.arena, rank)), px + 8, 92, pw - 16,
                       230, 210, 0, 120);
        if (g_draw_ac) {
            int parts[SLOT_COUNT];
            for (int s = 0; s < SLOT_COUNT; ++s) parts[s] = f->parts[s];
            g_draw_ac(parts, f->colours, px + 8, 92, pw - 16, 230, 1.0f, A.spin, 0, 1.0f);
        }
        char c[24];
        const acr_gen1_arena_ladder *l = &A.s.a[A.arena];
        const int d = acr_gen1_arena_holder(&A.s, A.arena, rank);
        num((long)f->prize_money, c, sizeof c);
        if (!(d && l->beaten[d - 1])) {   /* paid on the first win only: nothing shown after */
            SDL_snprintf(line, sizeof line, "PRIZE      %s C", c);
            acr_hud_text(px + 12, 336, line, WHITE);
        }
        int pc, pi;
        if ((A.arena == 5 || A.arena == 6)) {
            const acr_gen1_rogue_foe *top = acr_gen1_arena_foe(&A.w, A.arena, 1);
            if (d == 1 && top && !l->set_given && acr_gen1_arena_prize_part(top->prize_part, &pc, &pi)) {
                SDL_snprintf(line, sizeof line, "BEAT ALL:  %s", part_name(pc, pi));
                acr_hud_text(px + 12, 352, line, GOOD);
            }
        } else if (d && !l->part_given[d - 1] && acr_gen1_arena_prize_part(f->prize_part, &pc, &pi)) {
            SDL_snprintf(line, sizeof line, "PART       %s", part_name(pc, pi));
            acr_hud_text(px + 12, 352, line, GOOD);
        }
        const acr_gen1_arena_why why = why_at(rank);
        if (why != ACR_GEN1_ARENA_OK) acr_hud_text(px + 12, 372, acr_gen1_arena_why_text(why), DIM);
        else if (acr_gen1_arena_is_rematch(&A.s, A.arena, rank)) acr_hud_text(px + 12, 372, "DEFEATED.  A: REMATCH (NO PRIZE)", GOOD);
        else acr_hud_text(px + 12, 372, "A: CHALLENGE", GOOD);
        if (f->ai.rebel) acr_hud_text(px + 12, 388, "HUMAN PLUS", BAD);
    }
    message(hud_w, elapsed_ms);
    hints(hud_w, A.arena == 0 ? "UP/DOWN: OPPONENT   L1/R1: RANKED / SUB ARENA   A: CHALLENGE   B: BACK"
                              : "UP/DOWN: OPPONENT   A: CHALLENGE   B: BACK");
}

static void draw_ask(float hud_w, int elapsed_ms) {
    draw_ladder(hud_w, elapsed_ms);
    const acr_gen1_rogue_foe *f = foe_at(cursor_rank());
    char name[48], line[96];
    foe_name(f, name, sizeof name);
    SDL_snprintf(line, sizeof line, "%s %s?", acr_gen1_arena_is_rematch(&A.s, A.arena, cursor_rank()) ? "REMATCH" : "CHALLENGE", name);
    const float w = 8.0f * (float)SDL_strlen(line) + 48 > 360 ? 8.0f * (float)SDL_strlen(line) + 48 : 360;
    const float x = hud_w * 0.5f - w * 0.5f, y = 180;
    panel(x, y, w, 96);
    acr_hud_text(x + 24, y + 14, line, WHITE);
    SDL_snprintf(line, sizeof line, "STAGE %s   NO TIME LIMIT", stage_name(stage_now()));
    acr_hud_text(x + 24, y + 32, line, DIM);
    for (int k = 0; k < 2; ++k) {
        const float bx = x + w * 0.5f + (k ? 20 : -100);
        if (A.ask == k) row_bar(bx, y + 64, 80);
        if (A.ask == k) acr_hud_text(bx + 16, y + 64, k ? "NO" : "YES", HILITE);
        else acr_hud_text(bx + 16, y + 64, k ? "NO" : "YES", WHITE);
    }
}

static void draw_result(float hud_w) {
    header(hud_w, A.arena == 0 ? "ARENA" : "EX ARENA");
    const float w = 440, x = hud_w * 0.5f - w * 0.5f, y = 110;
    panel(x, y, w, 230);
    char line[96], c[24];
    const char *word = A.pay.won ? "YOU WIN" : "YOU LOSE";
    const float wx = x + w * 0.5f - 24.0f * (float)SDL_strlen(word) * 0.5f;
    if (A.pay.won) acr_hud_text_size(wx, y + 18, word, 24, GOOD, 255);
    else acr_hud_text_size(wx, y + 18, word, 24, BAD, 255);
    acr_hud_text(x + 24, y + 62, A.pay_foe, WHITE);
    if (A.pay.rematch) {
        SDL_snprintf(line, sizeof line, "REMATCH: RANK STAYS %d", A.pay.old_rank);
        acr_hud_text(x + 24, y + 86, line, WHITE);
        acr_hud_text(x + 24, y + 110, "NO PRIZE IN A REMATCH. GOOD FIGHT.", DIM);
    } else if (A.pay.won) {
        SDL_snprintf(line, sizeof line, "RANK  %d  >  %d", A.pay.old_rank, A.pay.new_rank);
        acr_hud_text(x + 24, y + 86, line, HILITE);
        if (A.pay.money) {   /* a repeat win over the same opponent: no prize line */
            num((long)A.pay.money, c, sizeof c);
            SDL_snprintf(line, sizeof line, "PRIZE MONEY   %s C", c);
            acr_hud_text(x + 24, y + 110, line, WHITE);
        }
        if (A.pay.prize_id >= 0) {
            acr_hud_text(x + 24, y + 138, "SPECIAL PARTS OBTAINED", GOOD);
            if (A.pay.part_cat >= 0) acr_hud_text(x + 24, y + 154, part_name(A.pay.part_cat, A.pay.part_index), HILITE);
            else acr_hud_text(x + 24, y + 154, "(A PART THIS BUILD DOESN'T HAVE YET)", DIM);
        }
        if (A.pay.new_rank == 1) acr_hud_text(x + 24, y + 182, "YOU ARE THE TOP OF THIS ARENA.", HILITE);
    } else {
        SDL_snprintf(line, sizeof line, "RANK  %d", A.pay.old_rank);
        acr_hud_text(x + 24, y + 86, line, WHITE);
        acr_hud_text(x + 24, y + 110, "NO PRIZE. REPAIR, REBUILD, TRY AGAIN.", DIM);
    }
    hints(hud_w, "A: GO ON");
}

void arena_draw(float hud_w, int elapsed_ms) {
    A.spin += (float)elapsed_ms * 0.0008f;
    switch (A.screen) {
    case AS_PICK: draw_pick(hud_w); message(hud_w, elapsed_ms); break;
    case AS_LADDER: draw_ladder(hud_w, elapsed_ms); break;
    case AS_ASK: draw_ask(hud_w, elapsed_ms); break;
    case AS_VS:
        acr_hud_rect(0, 0, hud_w, 480, 8, 10, 14, 255, 1);
        vs_draw(hud_w, elapsed_ms);
        break;
    case AS_RESULT: draw_result(hud_w); break;
    }
}
