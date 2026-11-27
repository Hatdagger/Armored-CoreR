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
    int want_import;                     /* the hub's PP ARENA with no disc yet: ask for it */
    int only;                            /* opened from a hub entry: 0 the whole list, 1 one ladder (B leaves), 2 Ex Arenas only */
    int arena, page, lcur, ltop;         /* AS_LADDER: arena, page (arena 0: 0 ranked, 1 sub), row, scroll */
    int ask;                             /* (unused: the challenge window is the stage select now) */
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

void arena_say(const char *t) { say(t); A.err = 0; }

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

size_t arena_write_text(char *buf, size_t size) { return acr_gen1_arena_write(&A.s, buf, size); }

int arena_read_text(const char *text) {
    acr_gen1_arena s;
    if (!text || !acr_gen1_arena_read(&s, text)) return 0;
    A.s = s;
    acr_gen1_arena_sync(&A.s, &A.w);
    save();
    return 1;
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

/* Test switch until missions count: ACR_ARENA_ABSENT="0:12,5:3" marks
 * (arena : disc rank) as unable to fight (disc rank = the rank on a new
 * game). Kept in arena.toml. */
static void absent_from_env(void) {
    const char *e = SDL_getenv("ACR_ARENA_ABSENT");
    if (!e) return;
    int changed = 0;
    while (*e) {
        char *end;
        const long a = SDL_strtol(e, &end, 10);
        if (end == e || *end != ':') break;
        e = end + 1;
        const long d = SDL_strtol(e, &end, 10);
        if (end == e) break;
        e = end;
        const int was = a >= 0 && a < ACR_GEN1_ARENA_COUNT && d >= 1 && d <= ACR_GEN1_ARENA_PLACES ? A.s.a[a].absent[d - 1] : -1;
        if (was >= 0 && acr_gen1_arena_set_absent(&A.s, (int)a, (int)d) && A.s.a[a].absent[d - 1] != was) changed = 1;
        while (*e == ',' || *e == ' ') ++e;
    }
    if (changed) save();
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
    absent_from_env();
    A.ok = A.s.a[ACR_GEN1_ARENA_MOA].places > 0 || A.s.a[ACR_GEN1_ARENA_PP].places > 0;
    A.screen = AS_PICK;
    return A.ok;
}

int arena_reload(void) {
    A.w.foes = demo_rogue_roster(A.import_dir, A.roster, ACR_GEN1_ROGUE_ROSTER, A.names);
    load();
    A.ok = A.s.a[ACR_GEN1_ARENA_MOA].places > 0 || A.s.a[ACR_GEN1_ARENA_PP].places > 0;
    return A.ok;
}

void arena_new_game(void) {
    acr_gen1_arena_new(&A.s, &A.w);
    save();
}

void arena_open(void) { arena_open_at(0); }

static int enter_row(int row);

/* row < 0: the whole arena list (main menu ARENA). The hub's own entries
 * open their arena: 0 PP ARENA, 1 MoA SUB-ARENA, 2 MoA ARENA go straight
 * to that ladder (B goes back to the hub); 3 (EX ARENA) opens a list of
 * just the Ex Arenas (user, 2026-11-27). */
void arena_open_at(int row) {
    A.screen = AS_PICK;
    A.message[0] = 0;
    A.message_ms = 0;
    A.only = 0;
    A.cur = 0;
    if (row < 0 || row > ACR_GEN1_ARENA_COUNT) return;
    if (row >= 3) { A.only = 2; A.cur = 3; return; }
    A.cur = row;
    /* Locked, or no data yet: stay on the list on that row, saying why. */
    const int e = enter_row(row);
    if (e == 1) A.only = 1;
    if (e < 0) A.want_import = 1;   /* no Phantasma disc yet: pick it now */
}

int arena_take_import(void) { const int w = A.want_import; A.want_import = 0; return w; }

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

/* Whether the ladder rules let this opponent be fought (the next rank up, a
 * rematch, anyone in the sub arena), as in the game: their names are yellow.
 * Leg type and an incomplete AC are about your build: still yellow, and A
 * says why. */
static int fightable(int rank) {
    const acr_gen1_arena_why why = why_at(rank);
    if (acr_gen1_arena_absent_at(&A.s, A.arena, rank) == ACR_GEN1_ARENA_ABSENT_DONE) return 0;
    return why == ACR_GEN1_ARENA_OK || why == ACR_GEN1_ARENA_WRONG_LEGS || why == ACR_GEN1_ARENA_INCOMPLETE;
}

/* The rows of the ladder page: first rank and count. */
static void page_range(int *first, int *count) {
    const int places = A.s.a[A.arena].places;
    if (A.arena == ACR_GEN1_ARENA_MOA && places > ACR_GEN1_ARENA_RANKED) {
        /* MoA ARENA (page 0) ranks 1-15, with the player's row 16 while
         * they wait at the top of the sub arena; MoA SUB-ARENA (page 1)
         * ranks 16-50. Two rows of the list, one ladder underneath. */
        const int me = acr_gen1_arena_rank(&A.s, A.arena);
        if (A.page == 0) { *first = 1; *count = ACR_GEN1_ARENA_RANKED + (me == ACR_GEN1_ARENA_RANKED + 1); }
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

const acr_gen1_rogue_foe *arena_find_ac(const char *ac) {
    for (int i = 0; ac && i < A.w.foes; ++i)
        if (!SDL_strcasecmp(A.roster[i].ac, ac)) return &A.roster[i];
    return NULL;
}

static const char *section_title(int a, int sub) {
    if (a == ACR_GEN1_ARENA_PP) return "PP ARENA";
    if (a == ACR_GEN1_ARENA_MOA) return sub ? "MoA SUB-ARENA" : "MoA ARENA";
    return "EX ARENA";
}

/* ---- the VS screen (vs_screen.c, shared with Rogue Arena) ----
 * The Arena's is the simpler one (user, 2026-11-25): both ACs, their RANK
 * badges and names, the opponent's emblem, VS, a 3 count and the
 * shutters; no strip in the bottom band. */

static void vs_start(void) {
    const int a = A.fight_arena, rank = A.fight_rank, me = acr_gen1_arena_rank(&A.s, a);
    const int rematch = acr_gen1_arena_is_rematch(&A.s, a, rank);
    SDL_snprintf(A.vs_right, sizeof A.vs_right, "%s",
                 a == ACR_GEN1_ARENA_PP ? "PROJECT PHANTASMA" : a == ACR_GEN1_ARENA_MOA ? "MASTER OF ARENA"
                                                                 : acr_gen1_arena_name(a));
    vs_setup s;
    SDL_zero(s);
    s.fight = &A.fight;
    s.mode = section_title(a, acr_gen1_arena_is_sub(a, rank));
    s.right = A.vs_right;
    s.band[0] = 0xEC; s.band[1] = 0xA8; s.band[2] = 0x24;
    s.rank[0] = me;
    s.rank[1] = rank;
    /* The player's emblem: none yet (the garage has no EDIT EMBLEM). */
    s.emblem[1] = vs_emblem(A.import_dir, a, acr_gen1_arena_holder(&A.s, a, rank));
    s.tag = rematch ? "REMATCH" : NULL;
    s.stage = stage_name(A.fight.stage);
    s.hint = "ENTER / A  SORTIE NOW    ESC / B  BACK TO THE LADDER";
    s.count = 3;
    vs_open(&s);
}

/* "Opponent unavailable, default win.": no fight, up one rank, no pay. */
static void default_win(int rank) {
    foe_name(foe_at(rank), A.pay_foe, sizeof A.pay_foe);
    if (!acr_gen1_arena_default_win(&A.s, A.arena, rank, &A.pay)) return;
    save();
    A.screen = AS_RESULT;
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
/* (the stage is picked in the challenge window: user, 2026-11-27) */
enum { PR_ARENAS = ACR_GEN1_ARENA_COUNT + 1, PR_BACK = PR_ARENAS, PR_COUNT };
enum { ROW_PP = 0, ROW_MOA_SUB = 1, ROW_MOA = 2, ROW_EX = 3 };
/* The pick list's first row: the Ex-only list starts at the Ex Arenas. */
static int pick_first(void) { return A.only == 2 ? ROW_EX : 0; }

/* The list's rows: PP ARENA (Project Phantasma's, the main one), then
 * Master of Arena's main ladder as its two sections, MoA SUB-ARENA (the
 * unranked 16-50) and MoA ARENA (ranked 1-15), then its Ex Arenas. */
static int row_arena(int row) {
    if (row == ROW_PP) return ACR_GEN1_ARENA_PP;
    if (row <= ROW_MOA) return ACR_GEN1_ARENA_MOA;
    return row - 2;
}
static int row_page(int row) { return row == ROW_MOA_SUB ? 1 : 0; }
static int arena_row(int a, int page) {
    if (a == ACR_GEN1_ARENA_PP) return ROW_PP;
    if (a == ACR_GEN1_ARENA_MOA) return page ? ROW_MOA_SUB : ROW_MOA;
    return a + 2;
}
/* MoA ARENA opens at the top of the sub arena (rank 16). */
static int moa_ranked_open(void) {
    return acr_gen1_arena_rank(&A.s, ACR_GEN1_ARENA_MOA) <= ACR_GEN1_ARENA_RANKED + 1;
}
static acr_gen1_arena_why row_open(int row) {
    const acr_gen1_arena_why why = acr_gen1_arena_open(&A.s, row_arena(row));
    if (why == ACR_GEN1_ARENA_OK && row == ROW_MOA && !moa_ranked_open()) return ACR_GEN1_ARENA_LOCKED;
    return why;
}

static void ladder_home(void);
/* Open the ladder for a list row: 1 opened, 0 refused (said why),
 * -1 Phantasma's row with no disc yet. */
static int enter_row(int row) {
    const int ar = row_arena(row);
    if (ar == ACR_GEN1_ARENA_PP && !A.s.a[ar].places) return -1;
    const acr_gen1_arena_why why = row_open(row);
    if (why != ACR_GEN1_ARENA_OK && !SDL_getenv("ACR_ARENA_UNLOCK")) { say(acr_gen1_arena_why_text(why)); return 0; }
    if (!A.s.a[ar].places) { say(acr_gen1_arena_why_text(ACR_GEN1_ARENA_NO_DATA)); return 0; }
    A.arena = ar;
    A.page = row_page(row);
    ladder_home();
    A.screen = AS_LADDER;
    return 1;
}

/* Whether a hub arena entry has anything to open right now (the hub
 * crosses out the rest): 0-2 its ladder, 3 any Ex Arena. */
int arena_hub_available(int row) {
    if (!A.ok) return 0;
    if (SDL_getenv("ACR_ARENA_UNLOCK")) return 1;
    if (row >= ROW_EX) {
        for (int r = ROW_EX; r < PR_ARENAS; ++r)
            if (row_open(r) == ACR_GEN1_ARENA_OK && A.s.a[row_arena(r)].places) return 1;
        return 0;
    }
    if (row == ROW_PP && !A.s.a[ACR_GEN1_ARENA_PP].places) return 1;   /* pressing it adds the disc */
    return row >= 0 && A.s.a[row_arena(row)].places && row_open(row) == ACR_GEN1_ARENA_OK;
}

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
        A.cur = pick_first() + step(A.cur - pick_first(), PR_COUNT - pick_first(), a);
        if (a == GA_CANCEL) return AC_LEAVE;
        if (a != GA_OK) return AC_NONE;
        if (A.cur == PR_BACK) return AC_LEAVE;
        /* No Phantasma arena yet: its row asks for the disc. */
        if (enter_row(A.cur) < 0) return AC_IMPORT_PP;
        return AC_NONE;
    case AS_LADDER: {
        int first, count;
        page_range(&first, &count);
        if (a == GA_PREV_TAB || a == GA_NEXT_TAB) { A.snd = -2; return AC_NONE; }
        if (a == GA_UP || a == GA_DOWN) { A.lcur = step(A.lcur, count, a); scroll_to_cursor(); return AC_NONE; }
        if (a == GA_LEFT || a == GA_RIGHT) {   /* a page of rows at a time */
            A.lcur += a == GA_LEFT ? -LADDER_ROWS : LADDER_ROWS;
            if (A.lcur < 0) A.lcur = 0;
            if (A.lcur >= count) A.lcur = count - 1;
            scroll_to_cursor();
            return AC_NONE;
        }
        if (a == GA_CANCEL) {
            if (A.only == 1) return AC_LEAVE;   /* opened from its hub entry: back to the hub */
            A.screen = AS_PICK; A.cur = arena_row(A.arena, A.page); return AC_NONE;
        }
        if (a != GA_OK) return AC_NONE;
        const int rank = cursor_rank();
        const acr_gen1_arena_why why = why_at(rank);
        if (!fightable(rank)) { A.snd = -3; return AC_NONE; }   /* not available yet: nothing happens */
        if (why != ACR_GEN1_ARENA_OK) { say(acr_gen1_arena_why_text(why)); return AC_NONE; }
        if (!A.nstages) { say("RE-IMPORT YOUR DISC: NO STAGES FOUND."); return AC_NONE; }
        A.ask = 0;
        A.screen = AS_ASK;
        return AC_NONE;
    }
    case AS_ASK:   /* the challenge window: Left / Right pick the stage, A fights, B backs out */
        if (a == GA_LEFT || a == GA_RIGHT) { cycle_stage(a == GA_LEFT ? -1 : 1); return AC_NONE; }
        if (a == GA_CANCEL) { A.screen = AS_LADDER; return AC_NONE; }
        if (a != GA_OK) { A.snd = -3; return AC_NONE; }
        {   /* the game checks the opponent is there after YES (FDAT 206 0x800BF43C) */
            const int rank = cursor_rank(), absent = acr_gen1_arena_absent_at(&A.s, A.arena, rank);
            if (absent == ACR_GEN1_ARENA_ABSENT) { default_win(rank); return AC_NONE; }
            if (absent == ACR_GEN1_ARENA_ABSENT_DONE) { say("OPPONENT IS UNABLE TO COMPETE."); A.screen = AS_LADDER; return AC_NONE; }
        }
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
        /* Topped the sub arena: on to MoA ARENA. */
        if (A.arena == ACR_GEN1_ARENA_MOA && A.page == 1 && moa_ranked_open()) {
            A.page = 0;
            say("MoA ARENA IS OPEN.");
        }
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
    const int first = pick_first(), shown = PR_COUNT - first;
    panel(x - 16, 56, w + 32, 20 * shown + 40);
    acr_hud_text(x, 64, first ? "EX ARENA" : "ARENA", DIM);
    text_right(x + w - 120, 64, "RANK", DIM);
    text_right(x + w, 64, "STATUS", DIM);
    for (int i = first; i < PR_COUNT; ++i) {
        const float y = 86 + 20.0f * (float)(i - first) + (i >= PR_BACK ? 10 : 0);
        const int sel = A.cur == i;
        if (sel) row_bar(x, y, w + 12);
        char line[64];
        if (i < PR_ARENAS) {
            const int ar = row_arena(i);
            const acr_gen1_arena_why why = row_open(i);
            const int open = why == ACR_GEN1_ARENA_OK;
            const char *name = i == ROW_MOA_SUB ? "MoA SUB-ARENA" : acr_gen1_arena_name(ar);
            if (ar == ACR_GEN1_ARENA_PP && !A.s.a[ar].places) {
                acr_hud_text(x, y, name, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
                text_right(x + w, y, "A: ADD PROJECT PHANTASMA DISC", sel ? 255 : 150, sel ? 220 : 160, sel ? 120 : 170);
                continue;
            }
            if (ar == ACR_GEN1_ARENA_PP || ar == ACR_GEN1_ARENA_MOA) {
                if (open) acr_hud_text(x, y, name, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
                else acr_hud_text(x, y, name, DIM);
            } else {
                SDL_snprintf(line, sizeof line, "EX ARENA  %s", name);
                if (open) acr_hud_text(x, y, line, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
                else acr_hud_text(x, y, line, DIM);
            }
            const int me = acr_gen1_arena_rank(&A.s, ar);
            const int sub_done = i == ROW_MOA_SUB && me <= ACR_GEN1_ARENA_RANKED + 1;
            if (A.s.a[ar].places) {
                if (i == ROW_MOA) {
                    if (me <= ACR_GEN1_ARENA_RANKED) SDL_snprintf(line, sizeof line, "%d / %d", me, ACR_GEN1_ARENA_RANKED);
                    else SDL_snprintf(line, sizeof line, "-- / %d", ACR_GEN1_ARENA_RANKED);
                } else if (i == ROW_MOA_SUB) {
                    /* unranked: just how many entries it has (35 with MoA's 50 places) */
                    (void)me;
                    SDL_snprintf(line, sizeof line, "%d", A.s.a[ar].places - ACR_GEN1_ARENA_RANKED);
                } else SDL_snprintf(line, sizeof line, "%d / %d", me, A.s.a[ar].places);
                text_right(x + w - 120, y, line, open ? 220 : 120, open ? 230 : 135, open ? 235 : 145);
            }
            const char *st = why == ACR_GEN1_ARENA_NO_DATA ? "NO DATA" : !open ? "LOCKED"
                           : sub_done ? "CLEARED" : me == 1 ? "CHAMPION" : "OPEN";
            if (open) text_right(x + w, y, st, GOOD); else text_right(x + w, y, st, DIM);
        } else {
            acr_hud_text(x, y, "BACK", sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
        }
    }
    /* ARENA REPORT for the row under the cursor. */
    if (A.cur < PR_ARENAS && A.s.a[row_arena(A.cur)].places) {
        const acr_gen1_arena_ladder *l = &A.s.a[row_arena(A.cur)];
        const float ry = 86 + 20.0f * (PR_COUNT - pick_first()) + 30;
        panel(x - 16, ry, w + 32, 50);
        char line[96];
        const int pct = l->challenges ? l->wins * 100 / l->challenges : 0;
        SDL_snprintf(line, sizeof line, "CHALLENGE %u   WIN %u (%d%%)   LOSE %u (%d%%)", l->challenges, l->wins, pct,
                     l->losses, l->challenges ? 100 - pct : 0);
        acr_hud_text(x, ry + 10, line, WHITE);
        int beaten = 0;
        for (int r = 1; r < l->places; ++r) beaten += l->beaten[r - 1] != 0;
        SDL_snprintf(line, sizeof line, "OPPONENTS BEATEN %d / %d   BEST RANK %d", beaten, l->places - 1,
                     l->best ? l->best : acr_gen1_arena_rank(&A.s, row_arena(A.cur)));
        acr_hud_text(x, ry + 26, line, DIM);
    }
    hints(hud_w, "UP/DOWN: CHOOSE   LEFT/RIGHT: STAGE   A: ENTER   B: BACK");
}


static void draw_ladder(float hud_w, int elapsed_ms) {
    char title[64];
    const int me = acr_gen1_arena_rank(&A.s, A.arena);
    if (A.arena == ACR_GEN1_ARENA_PP || A.arena == ACR_GEN1_ARENA_MOA)
        SDL_snprintf(title, sizeof title, "%s", section_title(A.arena, A.page));
    else SDL_snprintf(title, sizeof title, "EX ARENA  -  %s", acr_gen1_arena_name(A.arena));
    header(hud_w, title);
    int first, count;
    page_range(&first, &count);
    const float lx = 36, lw = 380;
    panel(lx - 16, 48, lw + 28, 26 + 22.0f * LADDER_ROWS);
    /* MoA SUB-ARENA is unranked: no rank numbers. */
    const int unranked = A.arena == ACR_GEN1_ARENA_MOA && A.page == 1;
    if (!unranked) acr_hud_text(lx, 56, "RANK", DIM);
    acr_hud_text(lx + 52, 56, "PILOT", DIM);
    for (int i = 0; i < LADDER_ROWS && A.ltop + i < count; ++i) {
        const int rank = first + A.ltop + i;
        const float y = 76 + 22.0f * (float)i;
        const int sel = A.ltop + i == A.lcur;
        if (sel) row_bar(lx, y, lw + 12);
        char line[64];
        if (!unranked) {
            SDL_snprintf(line, sizeof line, "%2d", rank);
            acr_hud_text(lx + 4, y, line, WHITE);
        }
        const acr_gen1_rogue_foe *f = foe_at(rank);
        if (f) {   /* its emblem, small */
            const uint32_t em = vs_emblem(A.import_dir, A.arena, f->rank);
            if (em) acr_hud_sprite(em, 64, 64, 0, 0, 64, 64, lx + 28, y - 4, 18, 18, 255, 255, 255, 255, ACR_HUD_ALPHA);
        }
        if (rank == me && A.h) SDL_snprintf(line, sizeof line, "%.20s", A.h->pilot_name[0] ? A.h->pilot_name : "YOU");
        else SDL_snprintf(line, sizeof line, "%.20s", f ? (f->pilot[0] ? f->pilot : f->ac) : "?");
        /* Just names: yellow = can be fought now, white = not yet; you in green. */
        if (rank == me) acr_hud_text(lx + 52, y, line, GOOD);
        else if (fightable(rank)) acr_hud_text(lx + 52, y, line, HILITE);
        else acr_hud_text(lx + 52, y, line, WHITE);
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
        if (unranked) SDL_snprintf(line, sizeof line, "SUB-ARENA (UNRANKED)");
        else SDL_snprintf(line, sizeof line, "YOUR RANK  %d / %d", me, A.s.a[A.arena].places);
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
        const int absent = acr_gen1_arena_absent_at(&A.s, A.arena, rank);
        num((long)f->prize_money, c, sizeof c);
        if (absent) {
            if (absent == ACR_GEN1_ARENA_ABSENT) acr_hud_text(px + 12, 336, "OPPONENT UNAVAILABLE", HILITE);
        } else if (!(d && l->beaten[d - 1])) {   /* paid on the first win only: nothing shown after */
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
        } else if (!absent && d && !l->part_given[d - 1] && acr_gen1_arena_prize_part(f->prize_part, &pc, &pi)) {
            SDL_snprintf(line, sizeof line, "PART       %s", part_name(pc, pi));
            acr_hud_text(px + 12, 352, line, GOOD);
        }
        const acr_gen1_arena_why why = why_at(rank);
        if (!fightable(rank)) { /* not available yet: no hint */ }
        else if (why != ACR_GEN1_ARENA_OK) acr_hud_text(px + 12, 372, acr_gen1_arena_why_text(why), DIM);
        else if (absent == ACR_GEN1_ARENA_ABSENT_DONE) acr_hud_text(px + 12, 372, "OPPONENT IS UNABLE TO COMPETE.", DIM);
        else if (acr_gen1_arena_is_rematch(&A.s, A.arena, rank)) acr_hud_text(px + 12, 372, "DEFEATED.  A: REMATCH (NO PRIZE)", GOOD);
        else acr_hud_text(px + 12, 372, "A: CHALLENGE", GOOD);
        if (f->ai.rebel) acr_hud_text(px + 12, 388, "HUMAN PLUS", BAD);
    }
    message(hud_w, elapsed_ms);
    hints(hud_w, "UP/DOWN: OPPONENT   A: CHALLENGE   B: BACK");
}

static void draw_ask(float hud_w, int elapsed_ms) {
    draw_ladder(hud_w, elapsed_ms);
    const acr_gen1_rogue_foe *f = foe_at(cursor_rank());
    char name[48], line[96];
    foe_name(f, name, sizeof name);
    SDL_snprintf(line, sizeof line, "%s %s?", acr_gen1_arena_is_rematch(&A.s, A.arena, cursor_rank()) ? "REMATCH" : "CHALLENGE", name);
    const float w = 8.0f * (float)SDL_strlen(line) + 48 > 360 ? 8.0f * (float)SDL_strlen(line) + 48 : 360;
    const float x = hud_w * 0.5f - w * 0.5f, y = 180;
    panel(x, y, w, 108);
    acr_hud_text(x + 24, y + 14, line, WHITE);
    acr_hud_text(x + 24, y + 32, "NO TIME LIMIT", DIM);
    /* the stage select in place of YES / NO */
    acr_hud_text(x + 24, y + 56, "STAGE", DIM);
    SDL_snprintf(line, sizeof line, "<  %s  >", A.nstages ? stage_name(stage_now()) : "NONE");
    const float lw = 8.0f * (float)SDL_strlen(line);
    row_bar(x + w * 0.5f - lw * 0.5f - 16, y + 56, lw + 32);
    acr_hud_text(x + w * 0.5f - lw * 0.5f, y + 56, line, HILITE);
    static const char k_keys[] = "A: FIGHT   B: BACK   LEFT/RIGHT: STAGE";
    acr_hud_text(x + w * 0.5f - 4.0f * (float)(sizeof k_keys - 1), y + 84, k_keys, DIM);
}

static void draw_result(float hud_w) {
    header(hud_w, section_title(A.arena, A.page));
    const float w = 440, x = hud_w * 0.5f - w * 0.5f, y = 110;
    panel(x, y, w, 230);
    char line[96], c[24];
    const char *word = A.pay.won ? "YOU WIN" : "YOU LOSE";
    const float wx = x + w * 0.5f - 24.0f * (float)SDL_strlen(word) * 0.5f;
    if (A.pay.won) acr_hud_text_size(wx, y + 18, word, 24, GOOD, 255);
    else acr_hud_text_size(wx, y + 18, word, 24, BAD, 255);
    acr_hud_text(x + 24, y + 62, A.pay_foe, WHITE);
    /* A sub-arena fight (unranked): no rank numbers in the result. */
    const int unranked = A.arena == ACR_GEN1_ARENA_MOA && A.pay.old_rank > ACR_GEN1_ARENA_RANKED;
    if (A.pay.default_win) {
        acr_hud_text(x + 24, y + 86, "OPPONENT UNAVAILABLE, DEFAULT WIN.", HILITE);
        if (!unranked) {
            SDL_snprintf(line, sizeof line, "RANK  %d  >  %d", A.pay.old_rank, A.pay.new_rank);
            acr_hud_text(x + 24, y + 110, line, HILITE);
        }
        if (A.pay.new_rank == 1) acr_hud_text(x + 24, y + 182, "YOU ARE THE TOP OF THIS ARENA.", HILITE);
    } else if (A.pay.rematch) {
        if (unranked) SDL_snprintf(line, sizeof line, "REMATCH");
        else SDL_snprintf(line, sizeof line, "REMATCH: RANK STAYS %d", A.pay.old_rank);
        acr_hud_text(x + 24, y + 86, line, WHITE);
        acr_hud_text(x + 24, y + 110, "NO PRIZE IN A REMATCH. GOOD FIGHT.", DIM);
    } else if (A.pay.won) {
        if (!unranked) {
            SDL_snprintf(line, sizeof line, "RANK  %d  >  %d", A.pay.old_rank, A.pay.new_rank);
            acr_hud_text(x + 24, y + 86, line, HILITE);
        }
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
        if (!unranked) {
            SDL_snprintf(line, sizeof line, "RANK  %d", A.pay.old_rank);
            acr_hud_text(x + 24, y + 86, line, WHITE);
        }
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
