#include "rogue.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <string.h>

#include "../../render/acr_render.h"
#include "rogue_sound.h"
#include "rogue_tiles_data.h"
#include "sfx.h"
#include "vs_screen.h"

#define SLOTS ACR_GEN1_ROGUE_SLOTS

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define GOOD 120, 230, 140
#define BAD 255, 90, 70

/* ---- state ---- */

enum { RS_MENU, RS_HUB, RS_STAGES, RS_SWAP, RS_SWAP_ASK, RS_VS, RS_RESULT, RS_PRIZE, RS_PRIZE_ASK,
       RS_REROLL_ASK, RS_LOST, RS_CLEARED, RS_SPECS,
       RS_MODE,                                  /* SOLO / CO-OP */
       RS_COOP };                                /* co-op's screens (rogue_coop.inc) */

/* The VS screen's strip of small boxes (AC1Recomp: MOA_VS_*). */
enum { VM_DOWN, VM_YOU, VM_NEXT, VM_AHEAD };
#define VS_MINI_MAX 8
typedef struct vs_mini {
    int kind, rank, top;
    float slash_at;                /* DOWN: when its slash starts; < -1 crossed out from the start */
    int parts[SLOT_COUNT];
    uint8_t colours[4][3];
} vs_mini;

static struct {
    int ok;                                   /* opponents loaded */
    char import_dir[1024];
    acr_gen1_rogue_foe roster[ACR_GEN1_ROGUE_ROSTER];
    int foes;
    acr_gen1_rogue_parts info;
    acr_gen1_catalog cat;                     /* prices in P, no shop */
    char (*names)[ACR_GEN1_PART_MAX][24];
    acr_gen1_rogue run;
    int live;                                 /* a run exists (file or new) */
    int screen, cur, top;
    int ask_cur, ask_n, ask_put[4];
    char message[96];
    int message_ms;
    char pilot[20], ac[20];
    /* the prize screen */
    acr_gen1_rogue_offer offer[3];
    int offers;
    int alt;                                  /* bottom row: 1 RE-ROLL, 0 PASS */
    /* the next fight */
    rogue_fight fight;
    int last_won;                             /* the result screen: 1 a win */
    int stages[16], nstages;
    uint32_t tile_tex[11];
    int err;                                  /* say() ran during this input: the back sound */
    int snd;                                  /* the sound this input asked for, -1 none */
    /* PART SPECS */
    demo_part_specs *specs;
    int specs_back;                           /* the screen it opened from (RS_PRIZE or RS_SWAP) */
    /* VS screen (AC1Recomp's, user 2026-11-17/18; vs_screen.c) */
    vs_mini mini[VS_MINI_MAX];
    int mini_n;
    char vs_belt[40];
    /* victory screen */
    float vic_t;
    int vic_words;
    int vic_parts[SLOT_COUNT];
    uint8_t vic_colours[4][3];
    char vic_rank[16], vic_pay[12], vic_beaten[64], vic_line[2][48];
    int vic_ghost;
    /* Co-op borrows the prize screen: `run` is a player's side while it
     * shows, the solo run waits in `solo`. */
    acr_gen1_rogue *borrow;
    acr_gen1_rogue solo;
    char whose[24];
} R;

/* Co-op's screens (rogue_coop.inc, included at the end). */
static void coop_open(void);
static rogue_command coop_input(garage_action a);
static void coop_draw(float hud_w, int elapsed_ms);
static void coop_prize_done(void);

void rogue_set_ac_drawer(rogue_ac_drawer fn) { vs_set_ac_drawer(fn); }

static acr_gen1_rogue_world world(void) {
    acr_gen1_rogue_world w = { R.roster, R.foes, &R.cat, &R.info };
    return w;
}

/* The stages the hub offers (user, 2026-11-19: only these four matter for
 * the current build; the rest of AC1Recomp's list comes later). Green
 * Silo is the default. */
static const struct { int id; const char *name; } k_stage[] = {
    { 27, "GREEN SILO" }, { 39, "OCEAN PLATFORM" }, { 25, "GARDEN PLAZA" }, { 36, "NIGHT CITY" },
};
#define NSTAGE ((int)(sizeof k_stage / sizeof k_stage[0]))

static const char *stage_name(int id) {
    for (int i = 0; i < NSTAGE; ++i) if (k_stage[i].id == id) return k_stage[i].name;
    return "STAGE";
}

static int stage_now(void) {
    for (int i = 0; i < R.nstages; ++i) if (k_stage[R.stages[i]].id == R.run.stage) return R.run.stage;
    return R.nstages ? k_stage[R.stages[0]].id : 27;
}

static void say(const char *t) {
    SDL_strlcpy(R.message, t, sizeof R.message);
    R.message_ms = 2500;
    R.err = 1;
}

/* ---- files ---- */

static void file_path(const char *name, char *out, size_t size) {
    const char *env = SDL_getenv("ACR_ROGUE_DIR");
    if (env && env[0]) { SDL_snprintf(out, size, "%s/%s", env, name); return; }
    char *pref = SDL_GetPrefPath("Hatdagger", "Armored-CoreR");
    SDL_snprintf(out, size, "%s%s", pref ? pref : "", name);
    SDL_free(pref);
}

static void save_run(void) {
    if (!R.live || R.borrow) return;
    static char text[32768];
    char path[1200];
    size_t n = acr_gen1_rogue_write(&R.run, text, sizeof text);
    file_path("rogue-run.toml", path, sizeof path);
    if (n < sizeof text) SDL_SaveFile(path, text, n);
    n = acr_gen1_save_write(&R.run.hangar, text, sizeof text);
    file_path("rogue-garage.toml", path, sizeof path);
    if (n < sizeof text) SDL_SaveFile(path, text, n);
}

static int load_run(void) {
    char path[1200];
    file_path("rogue-garage.toml", path, sizeof path);
    char *g = SDL_LoadFile(path, NULL);
    file_path("rogue-run.toml", path, sizeof path);
    char *t = SDL_LoadFile(path, NULL);
    int ok = 0;
    if (g && t) {
        acr_gen1_rogue r;
        SDL_zero(r);
        ok = acr_gen1_save_read(&r.hangar, g) && acr_gen1_rogue_read(&r, t);
        /* The ladder has to name opponents this import has. */
        const acr_gen1_rogue_world w = world();
        for (int i = 1; ok && i < SLOTS; ++i)
            if (r.slot[i].kind == ACR_GEN1_ROGUE_FOE && !acr_gen1_rogue_find(&w, r.slot[i].arena, r.slot[i].rank)) ok = 0;
        if (ok) R.run = r;
    }
    SDL_free(g);
    SDL_free(t);
    return ok;
}

/* A beaten ghost goes to rogue-ghosts/, marked as defeated (AC1Recomp: to
 * be shown in a fun way once the player reaches rank 1). */
static void save_defeated_ghost(const acr_gen1_rogue_foe *g, int ghost_attempt, int attempt) {
    char dir[1200], path[1400], text[2048];
    file_path("rogue-ghosts", dir, sizeof dir);
    SDL_CreateDirectory(dir);
    char hex[17];
    for (int i = 0; i < 16; ++i) hex[i] = "0123456789ABCDEF"[(R.run.seed >> (60 - 4 * i)) & 15u];
    hex[16] = 0;
    SDL_snprintf(path, sizeof path, "%s/%s-attempt%d-beaten-by-%d.toml", dir, hex, ghost_attempt + 1, attempt + 1);
    int n = SDL_snprintf(text, sizeof text,
        "# A Rogue Arena ghost, defeated.\ndefeated = 1\nseed = \"%s\"\npilot = \"%s\"\nac = \"%s\"\n"
        "rank = %d\nbeaten_by_attempt = %d\nparts = [", hex, g->pilot, g->ac, (int)g->rank, attempt + 1);
    for (int s = 0; s < SLOT_COUNT && n < (int)sizeof text - 8; ++s) n += SDL_snprintf(text + n, sizeof text - (size_t)n, "%s%d", s ? ", " : "", g->parts[s]);
    if (n < (int)sizeof text - 64) n += SDL_snprintf(text + n, sizeof text - (size_t)n, "]\noptions = %d\nhuman_plus = %d\n", g->options, g->ai.rebel);
    SDL_SaveFile(path, text, (size_t)n);
}

/* ---- setup ---- */

static int stage_exists(int id) {
    char path[1300];
    SDL_snprintf(path, sizeof path, "%s/stages/%d", R.import_dir, id);
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

int rogue_init(const char *import_dir, const acr_gen1_catalog *cat,
               char names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24]) {
    SDL_strlcpy(R.import_dir, import_dir, sizeof R.import_dir);
    R.names = names;
    /* The run's catalogue: nothing for sale (parts come from the prize
     * screen) and nothing sells back; prices stay in credits, which the
     * prize rules turn into P. */
    R.cat = *cat;
    R.cat.sell_percent = 0;
    for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c)
        for (int i = 0; i < ACR_GEN1_PART_MAX; ++i) R.cat.part[c][i].for_sale = 0;
    demo_rogue_parts(import_dir, &R.info);
    R.foes = demo_rogue_roster(import_dir, R.roster, ACR_GEN1_ROGUE_ROSTER, names);
    if (!R.specs) R.specs = SDL_calloc(1, sizeof *R.specs);
    if (R.specs) demo_part_specs_load(import_dir, R.specs);
    R.nstages = 0;
    for (int i = 0; i < NSTAGE && R.nstages < 16; ++i) if (stage_exists(k_stage[i].id)) R.stages[R.nstages++] = i;
    R.ok = R.foes >= SLOTS;
    R.live = R.ok && load_run();
    R.screen = RS_MODE;
    return R.ok;
}

static uint64_t fresh_seed(void) {
    const char *env = SDL_getenv("ACR_ROGUE_SEED");
    if (env && env[0]) return acr_gen1_rogue_seed_from_text(env);
    uint64_t s = SDL_GetTicksNS() ^ ((uint64_t)SDL_GetPerformanceCounter() << 17);
    return acr_gen1_rogue_next(&s);
}

static void to_hub(void) {
    if (R.borrow && !acr_gen1_rogue_prize_due(&R.run)) {   /* a co-op player's prize screen is done */
        *R.borrow = R.run;
        R.run = R.solo;
        R.borrow = NULL;
        R.screen = RS_COOP;
        coop_prize_done();
        return;
    }
    R.screen = acr_gen1_rogue_prize_due(&R.run) ? RS_PRIZE : RS_HUB;
    R.cur = R.top = 0;
    R.alt = 0;
    if (R.screen == RS_PRIZE) {
        const acr_gen1_rogue_world w = world();
        R.offers = acr_gen1_rogue_offers(&R.run, &w, R.offer);
        while (R.cur < R.offers && (int32_t)R.offer[R.cur].cost > R.run.hangar.credits) ++R.cur;
    }
    if (R.run.over) R.screen = RS_LOST;
    if (R.run.cleared) R.screen = RS_CLEARED;
}

static void new_arena(void) {
    const acr_gen1_rogue_world w = world();
    acr_gen1_rogue r;
    if (!acr_gen1_rogue_new(&r, &w, fresh_seed(), R.pilot, R.ac)) { say("NO ARENA COULD BE MADE FROM THIS IMPORT."); return; }
    r.stage = (uint8_t)stage_now();
    R.run = r;
    R.live = 1;
    save_run();
    to_hub();
}

void rogue_open(const char *pilot, const char *ac);
void rogue_open_coop(const char *pilot, const char *ac) {
    rogue_open(pilot, ac);
    if (R.ok) { R.cur = 1; coop_open(); }
}

void rogue_open(const char *pilot, const char *ac) {
    SDL_strlcpy(R.pilot, pilot && pilot[0] ? pilot : "RAVEN", sizeof R.pilot);
    SDL_strlcpy(R.ac, ac && ac[0] ? ac : "AC", sizeof R.ac);
    R.screen = RS_MODE;
    R.cur = 0;
    R.message[0] = 0;
}

/* ---- the start menu ---- */

enum { MI_CONTINUE, MI_RETRY, MI_NEW, MI_BACK };
static int menu_rows(int ids[4]) {
    int n = 0;
    if (R.live && !R.run.over && !R.run.cleared) ids[n++] = MI_CONTINUE;
    if (R.live && R.run.over) ids[n++] = MI_RETRY;
    ids[n++] = MI_NEW;
    ids[n++] = MI_BACK;
    return n;
}
static const char *const k_menu_name[4] = { "CONTINUE", "RETRY ARENA", "NEW ARENA", "BACK" };

/* ---- the next fight ---- */

static void name_of(const acr_gen1_rogue_foe *f, char *out, size_t size) {
    if (f->ac[0] && f->pilot[0]) SDL_snprintf(out, size, "%s / %s", f->ac, f->pilot);
    else SDL_snprintf(out, size, "%s", f->ac[0] ? f->ac : f->pilot);
}

static int next_foe(acr_gen1_rogue_foe *f) {
    const acr_gen1_rogue_world w = world();
    const acr_gen1_rogue_pick *p = acr_gen1_rogue_next_pick(&R.run);
    return p && acr_gen1_rogue_rung(&R.run, &w, p, f);
}

static int next_is_ghost(void) {
    const acr_gen1_rogue_pick *p = acr_gen1_rogue_next_pick(&R.run);
    return p && p->kind == ACR_GEN1_ROGUE_GHOST;
}

static void victory_open(void);
static void vs_start(void);

static int prepare_fight(void) {
    acr_gen1_rogue_foe f;
    if (!next_foe(&f)) return 0;
    rogue_fight *g = &R.fight;
    SDL_zero(*g);
    for (int s = 0; s < SLOT_COUNT; ++s) {
        g->parts[0][s] = R.run.hangar.fitted[s];
        g->parts[1][s] = f.parts[s];
    }
    memcpy(g->colours[0], R.run.hangar.colours, sizeof g->colours[0]);
    memcpy(g->colours[1], f.colours, sizeof g->colours[1]);
    g->plus[0] = R.run.plus ? 9 : 0;
    g->plus[1] = f.ai.rebel;
    g->ai = f.ai;
    g->stage = stage_now();
    SDL_snprintf(g->name[0], sizeof g->name[0], "%s / %s", R.run.hangar.ac_name, R.run.hangar.pilot_name);
    name_of(&f, g->name[1], sizeof g->name[1]);
    return 1;
}

const rogue_fight *rogue_fight_info(void) { return &R.fight; }

void rogue_fight_over(int result) {
    if (!R.live) return;
    if (result == 1) {
        acr_gen1_rogue_foe f;
        const acr_gen1_rogue_pick *p = acr_gen1_rogue_next_pick(&R.run);
        if (p && p->kind == ACR_GEN1_ROGUE_GHOST && p->rank < SLOTS && next_foe(&f))
            save_defeated_ghost(&f, R.run.ghost[p->rank].attempt, R.run.attempt);
        R.vic_ghost = next_is_ghost();
        acr_gen1_rogue_won(&R.run);
        R.last_won = 1;
        victory_open();
        R.screen = RS_RESULT;
    } else {
        acr_gen1_rogue_lost(&R.run);
        R.last_won = 0;
        R.screen = RS_LOST;
    }
    R.cur = 0;
    save_run();
}

/* ---- garage ---- */

garage_data rogue_garage_data(void) {
    garage_data d = { &R.run.hangar, &R.cat, R.names };
    return d;
}

void rogue_garage_done(void) {
    save_run();
    R.screen = RS_HUB;
}

int rogue_player_ac(int parts[SLOT_COUNT], uint8_t colours[4][3]) {
    if (!R.live) return 0;
    for (int s = 0; s < SLOT_COUNT; ++s) parts[s] = R.run.hangar.fitted[s];
    memcpy(colours, R.run.hangar.colours, 12);
    return 1;
}

/* ---- offers in words ---- */

static const char *cat_label(int cat) {
    static const char *const k[ACR_GEN1_CAT_COUNT] = { "CORE", "BOOSTER", "HEAD", "ARMS", "BLADE", "ARM WEAPON",
                                                       "BACK WEAPON", "GENERATOR", "FCS", "LEGS", "OPTION" };
    return cat >= 0 && cat < ACR_GEN1_CAT_COUNT ? k[cat] : "";
}

static const char *pname(int cat, int i) {
    if (i < 0 || i >= ACR_GEN1_PART_MAX || cat < 0 || cat >= ACR_GEN1_CAT_COUNT) return "NO EQUIP";
    return R.names && R.names[cat][i][0] ? R.names[cat][i] : "?";
}

static void offer_title(const acr_gen1_rogue_offer *o, char *name, size_t ns, char *cat, size_t cs) {
    switch (o->kind) {
    case ACR_GEN1_OFFER_HPLUS: SDL_strlcpy(name, "HUMAN PLUS", ns); SDL_strlcpy(cat, "UPGRADE  NO LOAD OR ENERGY LIMITS", cs); return;
    case ACR_GEN1_OFFER_OPTS: SDL_strlcpy(name, "ALL OPTIONS", ns); SDL_strlcpy(cat, "UPGRADE  EVERY OPTION PART, NO SLOT LIMIT", cs); return;
    case ACR_GEN1_OFFER_INCOME: SDL_strlcpy(name, "+1 P PER WIN", ns); SDL_strlcpy(cat, "UPGRADE  EVERY WIN PAYS 1 P MORE", cs); return;
    case ACR_GEN1_OFFER_SET:
        SDL_snprintf(name, ns, "ARMS SET");
        SDL_snprintf(cat, cs, "%s + %s + %s", pname(o->cat, o->index), pname(o->xcat[0], o->xindex[0]), pname(o->xcat[1], o->xindex[1]));
        return;
    default:
        SDL_strlcpy(name, pname(o->cat, o->index), ns);
        SDL_strlcpy(cat, cat_label(o->cat), cs);
        return;
    }
}

static void lost_text(const acr_gen1_rogue_offer *o, int put, char *out, size_t size) {
    out[0] = 0;
    for (int s = 0; s < SLOT_COUNT; ++s) {
        if (!(o->put_lost[put] & (1u << s))) continue;
        const int cat = acr_gen1_slot_category((acr_gen1_slot)s);
        const char *n = pname(cat, R.run.hangar.fitted[s]);
        if (SDL_strlen(out) + SDL_strlen(n) + 3 < size) { if (out[0]) SDL_strlcat(out, ", ", size); SDL_strlcat(out, n, size); }
    }
}

static const char *put_name(const acr_gen1_rogue_offer *o, int k) {
    if (o->nput < 2) return "EQUIP NOW";
    return o->put_slot[k] == ACR_GEN1_SLOT_BACK_L ? "EQUIP LEFT" : "EQUIP RIGHT";
}

/* ---- input ---- */

static int step(int cur, int n, garage_action a) {
    if (n <= 0) return 0;
    if (a == GA_UP) return (cur + n - 1) % n;
    if (a == GA_DOWN) return (cur + 1) % n;
    return cur;
}

enum { HR_NEXT, HR_SWAP, HR_STAGE, HR_GARAGE, HR_QUIT, HR_COUNT };

#define VIC_DONE 99.0f
static int victory_typed(void);

static rogue_command input(garage_action a) {
    const acr_gen1_rogue_world w = world();
    switch (R.screen) {
    case RS_MODE:
        if (!R.ok) { if (a == GA_OK || a == GA_CANCEL) return RC_LEAVE; return RC_NONE; }
        R.cur = step(R.cur, 3, a);
        if (a == GA_CANCEL || (a == GA_OK && R.cur == 2)) { R.cur = 0; return RC_LEAVE; }
        if (a == GA_OK && R.cur == 0) { R.screen = RS_MENU; R.cur = 0; }
        else if (a == GA_OK) coop_open();
        return RC_NONE;
    case RS_COOP:
        return coop_input(a);
    case RS_MENU: {
        int ids[4];
        const int n = menu_rows(ids);
        if (!R.ok) { if (a == GA_OK || a == GA_CANCEL) return RC_LEAVE; return RC_NONE; }
        R.cur = step(R.cur, n, a);
        if (R.cur >= n) R.cur = 0;
        if (a == GA_CANCEL) { R.screen = RS_MODE; R.cur = 0; return RC_NONE; }
        if (a != GA_OK) return RC_NONE;
        switch (ids[R.cur]) {
        case MI_CONTINUE: to_hub(); break;
        case MI_RETRY:
            if (acr_gen1_rogue_retry(&R.run, &w)) { save_run(); to_hub(); }
            break;
        case MI_NEW: new_arena(); break;
        default: R.screen = RS_MODE; R.cur = 0; return RC_NONE;
        }
        return RC_NONE;
    }
    case RS_HUB:
        R.cur = step(R.cur, HR_COUNT, a);
        if (a == GA_CANCEL) { R.cur = HR_QUIT; return RC_NONE; }
        if (a != GA_OK) return RC_NONE;
        switch (R.cur) {
        case HR_NEXT:
            if (acr_gen1_rogue_check(&R.run, &R.cat)) { say("YOUR AC CAN'T SORTIE AS IT IS: CHECK THE GARAGE."); break; }
            if (!R.nstages) { say("RE-IMPORT YOUR DISC: NO STAGES FOUND."); break; }
            if (!prepare_fight()) { say("THE NEXT OPPONENT COULD NOT BE LOADED."); break; }
            vs_start();
            R.screen = RS_VS;
            break;
        case HR_SWAP:
            if (!R.run.saved_n) { say("NO PARTS SAVED: TAKE A PRIZE AND SAVE IT FOR LATER."); break; }
            R.screen = RS_SWAP; R.cur = 0; R.top = 0;
            break;
        case HR_STAGE:
            R.screen = RS_STAGES;
            R.cur = 0;
            for (int i = 0; i < R.nstages; ++i) if (k_stage[R.stages[i]].id == stage_now()) R.cur = i;
            break;
        case HR_GARAGE: return RC_GARAGE;
        default: save_run(); R.screen = RS_MENU; R.cur = 0; return RC_LEAVE;
        }
        return RC_NONE;
    case RS_STAGES:
        R.cur = step(R.cur, R.nstages, a);
        if (a == GA_PREV_TAB || a == GA_NEXT_TAB) {   /* a stage at random */
            uint64_t s = SDL_GetTicksNS();
            R.cur = (int)acr_gen1_rogue_below(&s, (uint32_t)R.nstages);
            a = GA_OK;
        }
        if (a == GA_CANCEL) { R.screen = RS_HUB; R.cur = HR_STAGE; }
        if (a == GA_OK && R.nstages) {
            R.run.stage = (uint8_t)k_stage[R.stages[R.cur]].id;
            save_run();
            R.screen = RS_HUB; R.cur = HR_STAGE;
        }
        return RC_NONE;
    case RS_SWAP:
        if (a == GA_INFO) {
            if (R.cur < R.run.saved_n) { R.specs_back = RS_SWAP; R.screen = RS_SPECS; }
            else R.snd = -2;
            return RC_NONE;
        }
        R.cur = step(R.cur, R.run.saved_n + 1, a);
        if (a == GA_CANCEL || (a == GA_OK && R.cur == R.run.saved_n)) { R.screen = RS_HUB; R.cur = HR_SWAP; return RC_NONE; }
        if (a == GA_OK) {
            acr_gen1_rogue_offer o;
            if (!acr_gen1_rogue_saved_fit(&R.run, &w, R.cur, &o)) { say("IT CAN'T BE EQUIPPED WITH YOUR CURRENT PARTS."); return RC_NONE; }
            R.ask_n = 0;
            for (int k = 0; k < o.nput; ++k) R.ask_put[R.ask_n++] = k;
            R.ask_put[R.ask_n++] = -1;
            R.ask_cur = 0;
            R.screen = RS_SWAP_ASK;
        }
        return RC_NONE;
    case RS_SWAP_ASK:
        R.ask_cur = step(R.ask_cur, R.ask_n, a);
        if (a == GA_CANCEL || (a == GA_OK && R.ask_put[R.ask_cur] < 0)) { R.screen = RS_SWAP; return RC_NONE; }
        if (a == GA_OK) {
            if (acr_gen1_rogue_swap(&R.run, &w, R.cur, R.ask_put[R.ask_cur])) { say("EQUIPPED."); R.snd = SFX_UI_EQUIP; save_run(); }
            R.screen = R.run.saved_n ? RS_SWAP : RS_HUB;
            if (R.cur >= R.run.saved_n) R.cur = R.run.saved_n ? R.run.saved_n - 1 : HR_SWAP;
        }
        return RC_NONE;
    case RS_VS:
        if (vs_closing()) { R.snd = -2; return RC_NONE; }             /* closing: nothing to do */
        if (a == GA_CANCEL) { R.screen = RS_HUB; R.cur = HR_NEXT; return RC_NONE; }
        if (a == GA_OK) vs_skip();                                    /* skip the count: the shutters now */
        else R.snd = -2;
        return RC_NONE;
    case RS_RESULT:
        if (a != GA_OK && a != GA_CANCEL) { R.snd = -2; return RC_NONE; }
        if (!victory_typed()) { R.vic_t = VIC_DONE; R.snd = -2; return RC_NONE; }   /* first press: all of it */
        to_hub();
        return RC_NONE;
    case RS_PRIZE: {
        const int rows = R.offers + 1;
        if (a == GA_INFO) {
            if (R.cur < R.offers) { R.specs_back = RS_PRIZE; R.screen = RS_SPECS; }
            else R.snd = -2;
            return RC_NONE;
        }
        R.cur = step(R.cur, rows, a);
        if (R.cur == R.offers && (a == GA_LEFT || a == GA_RIGHT)) R.alt = !R.alt;
        if (a != GA_OK) return RC_NONE;
        if (R.cur == R.offers) {
            if (R.alt) {
                if (!acr_gen1_rogue_can_reroll(&R.run)) { say(R.run.reroll_win == R.run.prize_wins + 1u ? "ONE RE-ROLL PER WIN." : "NOT ENOUGH P."); return RC_NONE; }
                R.screen = RS_REROLL_ASK; R.ask_cur = 0;
                return RC_NONE;
            }
            acr_gen1_rogue_pass(&R.run);
            say("PASSED: +1 P.");
            R.snd = SFX_UI_OK;
            save_run();
            to_hub();
            return RC_NONE;
        }
        const acr_gen1_rogue_offer *o = &R.offer[R.cur];
        if ((int32_t)o->cost > R.run.hangar.credits) { say("NOT ENOUGH P."); return RC_NONE; }
        R.ask_n = 0;
        if (o->kind == ACR_GEN1_OFFER_PART || o->kind == ACR_GEN1_OFFER_SET) {
            for (int k = 0; k < o->nput; ++k) R.ask_put[R.ask_n++] = k;
            R.ask_put[R.ask_n++] = -1;              /* SAVE FOR LATER */
        } else {
            R.ask_put[R.ask_n++] = 0;               /* BUY */
        }
        R.ask_put[R.ask_n++] = -2;                  /* BACK */
        R.ask_cur = 0;
        R.screen = RS_PRIZE_ASK;
        return RC_NONE;
    }
    case RS_PRIZE_ASK: {
        R.ask_cur = step(R.ask_cur, R.ask_n, a);
        if (a == GA_CANCEL || (a == GA_OK && R.ask_put[R.ask_cur] == -2)) { R.screen = RS_PRIZE; return RC_NONE; }
        if (a != GA_OK) return RC_NONE;
        const acr_gen1_rogue_offer o = R.offer[R.cur];
        if (acr_gen1_rogue_take(&R.run, &w, &o, R.ask_put[R.ask_cur])) {
            say(R.ask_put[R.ask_cur] == -1 ? "SAVED FOR LATER: SWAP PARTS AT THE HUB." : "DONE.");
            R.snd = R.ask_put[R.ask_cur] >= 0 && (o.kind == ACR_GEN1_OFFER_PART || o.kind == ACR_GEN1_OFFER_SET)
                  ? SFX_UI_EQUIP : SFX_UI_OK;
            save_run();
            to_hub();
        }
        return RC_NONE;
    }
    case RS_REROLL_ASK:
        R.ask_cur = step(R.ask_cur, 2, a);
        if (a == GA_CANCEL || (a == GA_OK && R.ask_cur == 1)) { R.screen = RS_PRIZE; return RC_NONE; }
        if (a == GA_OK && acr_gen1_rogue_reroll(&R.run)) {
            save_run();
            to_hub();
        }
        return RC_NONE;
    case RS_LOST:
        R.cur = step(R.cur, 3, a);
        if (a != GA_OK) return RC_NONE;
        if (R.cur == 0) { if (acr_gen1_rogue_retry(&R.run, &w)) { save_run(); to_hub(); } }
        else if (R.cur == 1) new_arena();
        else { R.screen = RS_MENU; R.cur = 0; return RC_LEAVE; }
        return RC_NONE;
    case RS_CLEARED:
        R.cur = step(R.cur, 2, a);
        if (a != GA_OK) return RC_NONE;
        if (R.cur == 0) new_arena();
        else { R.screen = RS_MENU; R.cur = 0; return RC_LEAVE; }
        return RC_NONE;
    case RS_SPECS: {
        /* Up / down: the next prize (or saved part) with the windows open. */
        const int n = R.specs_back == RS_PRIZE ? R.offers : R.run.saved_n;
        if (a == GA_UP || a == GA_DOWN) { if (n > 1) R.cur = step(R.cur, n, a); else R.snd = -2; }
        else if (a == GA_INFO || a == GA_CANCEL) R.screen = R.specs_back;
        else R.snd = -2;
        return RC_NONE;
    }
    }
    return RC_NONE;
}

/* Every press gets the game's menu click (AC1Recomp's overlay sounds, from
 * the menu bank on the disc): the cursor on a move, OK, back on cancel or
 * when the press is refused, the garage's equip sound on an equip. */
rogue_command rogue_input(garage_action a) {
    if (a == GA_NONE) return RC_NONE;
    const int screen = R.screen, cur = R.cur, ask = R.ask_cur, alt = R.alt;
    R.err = 0;
    R.snd = -1;
    const rogue_command c = input(a);
    int k = R.snd;
    if (k == -1) {
        if (R.err) k = SFX_UI_BACK;
        else if (a == GA_OK) k = SFX_UI_OK;
        else if (a == GA_CANCEL) k = SFX_UI_BACK;
        else if (a == GA_INFO) k = R.screen != screen ? SFX_UI_OK : SFX_UI_BACK;
        else if (R.screen != screen || R.cur != cur || R.ask_cur != ask || R.alt != alt) k = SFX_UI_CURSOR;
    }
    if (k >= 0) sfx_ui(k);
    return c;
}

/* ---- drawing ---- */

#define PANEL_W 300.0f

static void panel(float x, float y, float w, float h) {
    acr_hud_rect(x, y, w, h, 10, 14, 20, 210, 1);
    acr_hud_rect(x, y, w, h, 70, 90, 100, 255, 0);
}

static void row(float x, float y, float w, const char *text, int selected, int dim) {
    if (selected) acr_hud_rect(x - 8, y - 3, w, 14, 60, 70, 40, 255, 1);
    if (dim) acr_hud_text(x, y, text, DIM);
    else if (selected) acr_hud_text(x, y, text, HILITE);
    else acr_hud_text(x, y, text, WHITE);
}

/* The hub's background: the belt pattern of the player's rank, tiled
 * (AC1Recomp's hub tiles, karate belt colours as the player climbs). */
static void background(float hud_w, int belt) {
    if (belt < 0 || belt >= MOA_HUB_RANK_TILE_COUNT) belt = 0;
    if (!R.tile_tex[belt]) {
        static uint8_t rgba[64 * 64 * 4];
        const uint16_t *clut = moa_hub_rank_clut[belt];
        for (int i = 0; i < 64 * 64; ++i) {
            const int idx = (moa_hub_rank_pix[belt][i / 2] >> ((i & 1) * 4)) & 15;
            const uint16_t c = clut[idx];
            /* 15-bit colour, brightened a little: the tiles were dimmed to
             * sit behind the game's own menus. */
            const int r = (c & 31) * 8 * 3 / 2, g = ((c >> 5) & 31) * 8 * 3 / 2, b = ((c >> 10) & 31) * 8 * 3 / 2;
            rgba[i * 4 + 0] = (uint8_t)(r > 255 ? 255 : r);
            rgba[i * 4 + 1] = (uint8_t)(g > 255 ? 255 : g);
            rgba[i * 4 + 2] = (uint8_t)(b > 255 ? 255 : b);
            rgba[i * 4 + 3] = c ? 255 : 0;
        }
        R.tile_tex[belt] = acr_render_texture(rgba, 64, 64, 1);
    }
    acr_hud_rect(0, 0, hud_w, 480, 8, 10, 14, 255, 1);
    for (float y = 0; y < 480; y += 96)
        for (float x = 0; x < hud_w; x += 96)
            acr_hud_sprite(R.tile_tex[belt], 64, 64, 0, 0, 64, 64, x, y, 96, 96, 255, 255, 255, 255, ACR_HUD_ALPHA);
}

static int rank_now(void) { return SLOTS - R.run.wins; }

static void header(float hud_w, const char *title) {
    char line[96];
    acr_hud_text(24, 16, title, HILITE);
    if (R.live) {
        SDL_snprintf(line, sizeof line, "BALANCE %4ld P", (long)R.run.hangar.credits);
        acr_hud_text(hud_w - 24 - 8.0f * (float)SDL_strlen(line), 16, line, WHITE);
    }
}

static void foe_line(char *out, size_t size) {
    acr_gen1_rogue_foe f;
    if (!next_foe(&f)) { SDL_snprintf(out, size, "NO OPPONENT LEFT"); return; }
    char n[48];
    name_of(&f, n, sizeof n);
    SDL_snprintf(out, size, "NEXT  RANK %d  %s%s", rank_now() - 1, n, next_is_ghost() ? "  (GHOST)" : "");
}

static void part_lines(float x, float y, const acr_gen1_rogue_offer *o) {
    char line[96];
    if (o->kind != ACR_GEN1_OFFER_PART && o->kind != ACR_GEN1_OFFER_SET) return;
    const acr_gen1_catalog_part *p = &R.cat.part[o->cat][o->index];
    SDL_snprintf(line, sizeof line, "WEIGHT %5u   ENERGY DRAIN %5u", (unsigned)p->stats.weight, (unsigned)p->stats.energy_drain);
    acr_hud_text(x, y, line, WHITE);
    const unsigned atk = R.info.attack[o->cat][o->index];
    if (atk) { SDL_snprintf(line, sizeof line, "ATTACK %5u", atk); acr_hud_text(x, y + 14, line, WHITE); }
    if (o->nput) {
        char lost[80];
        lost_text(o, 0, lost, sizeof lost);
        SDL_snprintf(line, sizeof line, "REPLACES: %s", lost[0] ? lost : "NOTHING (AN EMPTY SLOT)");
        acr_hud_text(x, y + 28, line, DIM);
        if (o->nput > 1) {
            lost_text(o, 1, lost, sizeof lost);
            SDL_snprintf(line, sizeof line, "    OR: %s", lost[0] ? lost : "NOTHING");
            acr_hud_text(x, y + 42, line, DIM);
        }
    }
}

/* ---- shared drawing bits for the VS, victory and PART SPECS screens ---- */

static void text_at(float x, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b) { vs_text_at(x, y, t, size, r, g, b); }
static float text_w(const char *t, float size) { return vs_text_w(t, size); }
static void text_mid(float cx, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b) { vs_text_mid(cx, y, t, size, r, g, b); }
static void text_right(float rx, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b) { vs_text_right(rx, y, t, size, r, g, b); }
/* Text with a dark edge (YOU WIN). */
static void text_edged(float x, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b) {
    const float e = size / 16.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) acr_hud_text_size(x + dx * e, y + dy * e, t, size, 8, 8, 10, 255);
    acr_hud_text_size(x, y, t, size, r, g, b, 255);
}

static void gradient(float x, float y, float w, float h, const uint8_t c[3], float k0, float k1) { vs_gradient(x, y, w, h, c, k0, k1); }
static void frame(float x, float y, float w, float h, const uint8_t c[3], float t) { vs_frame(x, y, w, h, c, t); }
static void thick_line(float x0, float y0, float x1, float y1, float t, uint8_t r, uint8_t g, uint8_t b) { vs_thick_line(x0, y0, x1, y1, t, r, g, b); }
static void soft_dot(float cx, float cy, float rad, uint8_t r, uint8_t g, uint8_t b, uint8_t a) { vs_soft_dot(cx, cy, rad, r, g, b, a); }


static float ease_out(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - (1 - t) * (1 - t) * (1 - t); }

/* Belt colours (AC1Recomp k_belt_band) and names as the victory lines say them. */
static const uint8_t k_band[11][3] = {
    { 0xE1, 0xE1, 0xDC }, { 0xEC, 0xA8, 0x24 }, { 0xEC, 0x70, 0x1C }, { 0x40, 0xA8, 0x48 }, { 0x30, 0x70, 0xD0 },
    { 0x80, 0x48, 0xB8 }, { 0xCC, 0x28, 0x28 }, { 0x8C, 0x54, 0x2C }, { 0x36, 0x36, 0x3C }, { 0x8C, 0x1C, 0x1C },
    { 0xB0, 0x8C, 0x28 },
};
static const char *const k_belt_said[11] = {
    "WHITE", "YELLOW", "ORANGE", "GREEN", "BLUE", "PURPLE", "RED", "BROWN", "BLACK", "BLACK AND RED", "BLACK AND GOLD",
};
static int belt_of_rank(int rank) { return acr_gen1_rogue_belt(SLOTS - rank); }

static const uint8_t k_blue[3] = { 120, 170, 235 }, k_red[3] = { 235, 90, 80 }, k_grey_box[3] = { 110, 116, 132 };

static void draw_ac(const int parts[SLOT_COUNT], const uint8_t colours[4][3], float x, float y, float w, float h,
                    float frac, float yaw, int lying, float shade) {
    vs_draw_ac(parts, colours, x, y, w, h, frac, yaw, lying, shade);
}

/* ---- the VS screen (vs_screen.c, shared with the Arena) ----
 * Ladder of 19 (AC1Recomp: 49): its belts are two ranks, so the strip
 * along the bottom shows the last three ACs beaten (whatever their belt),
 * YOU, the next and the rest of the next one's belt; the last three beaten
 * are crossed out with a rifle shot on counts 3, 2, 1. */

static void foe_paint(const acr_gen1_rogue_foe *f, uint8_t c[4][3]) { SDL_memcpy(c, f->colours, 12); }

static void vs_strip(float hud_w, float sx, float t, const uint8_t band[3]);

static void vs_start(void) {
    R.mini_n = 0;
    const int my_rank = rank_now(), foe_rank = my_rank - 1;
    const int belt = belt_of_rank(foe_rank);
    int lo = foe_rank;
    while (lo > 1 && belt_of_rank(lo - 1) == belt) --lo;
    int hi = foe_rank;
    while (hi < SLOTS - 1 && belt_of_rank(hi + 1) == belt) ++hi;
    if (hi == lo) SDL_snprintf(R.vs_belt, sizeof R.vs_belt, "%s BELT  %d", acr_gen1_rogue_belt_name(belt), hi);
    else SDL_snprintf(R.vs_belt, sizeof R.vs_belt, "%s BELT  %d-%d", acr_gen1_rogue_belt_name(belt), hi, lo);
    const acr_gen1_rogue_world w = world();
    /* the beaten, oldest first (ranks above rank 20, the player's start) */
    int first = my_rank + 3;
    if (first > SLOTS - 1) first = SLOTS - 1;
    int beaten = 0;
    for (int r = first; r > my_rank && R.mini_n < VS_MINI_MAX; --r) {
        vs_mini *m = &R.mini[R.mini_n];
        acr_gen1_rogue_foe f;
        if (!acr_gen1_rogue_rung(&R.run, &w, &R.run.slot[SLOTS - r], &f)) continue;
        SDL_zero(*m);
        m->kind = VM_DOWN; m->rank = r;
        for (int s = 0; s < SLOT_COUNT; ++s) m->parts[s] = f.parts[s];
        foe_paint(&f, m->colours);
        ++R.mini_n;
        ++beaten;
    }
    /* the last three beaten are crossed out on counts 3, 2, 1 */
    for (int i = 0; i < beaten; ++i) {
        const int from_end = beaten - 1 - i;
        R.mini[i].slash_at = from_end >= 3 ? -9.0f : VS_C0 + (float)(4 - from_end) * VS_STEP;
    }
    if (R.mini_n < VS_MINI_MAX) {
        vs_mini *m = &R.mini[R.mini_n++];
        SDL_zero(*m);
        m->kind = VM_YOU; m->rank = my_rank;
        for (int s = 0; s < SLOT_COUNT; ++s) m->parts[s] = R.run.hangar.fitted[s];
        SDL_memcpy(m->colours, R.run.hangar.colours, 12);
    }
    for (int r = foe_rank; r >= lo && R.mini_n < VS_MINI_MAX; --r) {
        acr_gen1_rogue_foe f;
        if (!acr_gen1_rogue_rung(&R.run, &w, &R.run.slot[SLOTS - r], &f)) continue;
        vs_mini *m = &R.mini[R.mini_n++];
        SDL_zero(*m);
        m->kind = r == foe_rank ? VM_NEXT : VM_AHEAD;
        m->rank = r;
        m->top = r == lo && r != foe_rank;
        for (int s = 0; s < SLOT_COUNT; ++s) m->parts[s] = f.parts[s];
        foe_paint(&f, m->colours);
    }
    vs_setup s;
    SDL_zero(s);
    s.fight = &R.fight;
    s.mode = "ROGUE ARENA";
    s.right = R.vs_belt;
    SDL_memcpy(s.band, k_band[belt < 0 ? 0 : belt > 10 ? 10 : belt], 3);
    s.rank[0] = R.run.wins ? rank_now() : 0;
    s.rank[1] = foe_rank;
    /* the opponent's emblem (none for ghosts) */
    const acr_gen1_rogue_pick *p = acr_gen1_rogue_next_pick(&R.run);
    if (p && p->kind == ACR_GEN1_ROGUE_FOE) s.emblem[1] = vs_emblem(R.import_dir, p->arena, p->rank);
    s.for_rank = foe_rank;
    s.tag = next_is_ghost() ? "GHOST" : NULL;
    s.stage = stage_name(R.fight.stage);
    s.hint = "ENTER / A  SORTIE NOW    ESC / B  BACK TO THE HUB";
    s.shots = beaten < 3 ? beaten : 3;
    s.strip = vs_strip;
    vs_open(&s);
}

/* The strip of small boxes along the bottom. */
static void vs_strip(float hud_w, float sx, float t, const uint8_t band[3]) {
    const float cx = hud_w * 0.5f;
    char s[16];
    float total = 0;
    for (int i = 0; i < R.mini_n; ++i) total += (R.mini[i].top ? 124 : 100) * sx + (i ? 15 : 0);
    float x = cx - total * 0.5f;
    const float pulse = 0.5f + 0.5f * SDL_sinf(t * 6.0f);
    for (int i = 0; i < R.mini_n; ++i) {
        const vs_mini *m = &R.mini[i];
        const float w = (m->top ? 124 : 100) * sx, h = m->top ? 100 : 80, y = 431 - h;
        float gk = 0, sk = 0;
        if (m->kind == VM_DOWN) {
            const float after = t - (m->slash_at + 0.08f);
            gk = SDL_clamp(after / 0.15f, 0.0f, 1.0f);
            sk = SDL_clamp(after / 0.5f, 0.0f, 1.0f);
            if (m->slash_at < -1) gk = sk = 1;
        }
        const uint8_t *tint = m->kind == VM_YOU ? k_blue : m->kind == VM_AHEAD ? (m->top ? band : k_grey_box) : k_red;
        gradient(x, y, w, h, tint, 0.5f, 0.12f);
        draw_ac(m->parts, m->colours, x, y, w, h, 0.62f, m->kind == VM_YOU ? -0.35f : 0.35f, 0, 1.0f - 0.5f * gk);
        if (gk > 0) acr_hud_rect(x, y, w, h, 40, 40, 44, (uint8_t)(150 * gk), 1);   /* greyed */
        if (sk > 0)                                          /* smoking */
            for (int k = 0; k < 6; ++k) {
                const float u = SDL_fmodf(t * 0.45f + (float)k / 6.0f + (float)m->rank * 0.37f, 1.0f);
                soft_dot(x + w * (0.25f + 0.1f * (float)k) + u * 8, y + h * 0.7f - u * h * 0.6f, 8 + u * 14,
                         60, 58, 56, (uint8_t)(150 * sk * (1 - u)));
            }
        if (m->kind == VM_DOWN) {                            /* the slash, then it stays */
            float prog = m->slash_at < -1 ? 1.0f : (t - m->slash_at) / 0.08f;
            if (prog > 0) {
                if (prog > 1) prog = 1;
                const float x0 = x + 5, y0 = y + h - 5, x1 = x + w - 5, y1 = y + 5;
                const float xe = x0 + (x1 - x0) * prog, ye = y0 + (y1 - y0) * prog;
                thick_line(x0, y0, xe, ye, 3.0f, 20, 0, 0);
                thick_line(x0, y0, xe, ye, 1.7f, 215, 40, 34);
                if (m->slash_at >= -1 && t - m->slash_at >= 0 && t - m->slash_at < 0.16f) soft_dot(xe, ye, 7, 255, 235, 190, 220);
            }
        }
        uint8_t col[3];
        float th = 2;
        if (m->kind == VM_DOWN) {
            col[0] = (uint8_t)(150 + (80 - 150) * gk); col[1] = (uint8_t)(70 + (80 - 70) * gk); col[2] = (uint8_t)(64 + (86 - 64) * gk);
        } else if (m->kind == VM_YOU) SDL_memcpy(col, k_blue, 3);
        else if (m->kind == VM_NEXT) {
            const float k = 0.75f + 0.25f * pulse;
            col[0] = (uint8_t)(k_red[0] * k); col[1] = (uint8_t)(k_red[1] * k); col[2] = (uint8_t)(k_red[2] * k);
            th = 3;
        } else if (m->top) SDL_memcpy(col, band, 3);
        else { col[0] = 96; col[1] = 102; col[2] = 118; }
        frame(x, y, w, h, col, th);
        if (m->kind == VM_YOU) SDL_strlcpy(s, "YOU", sizeof s);
        else SDL_snprintf(s, sizeof s, "%d", m->rank);
        if (m->kind == VM_DOWN && gk >= 1) text_mid(x + w * 0.5f, y + h + 6, s, 10, 150, 156, 170);
        else if (m->kind == VM_AHEAD && m->top) text_mid(x + w * 0.5f, y + h + 6, s, 10, 255, 196, 72);
        else text_mid(x + w * 0.5f, y + h + 6, s, 10, 236, 236, 236);
        x += w + 15;
    }
}


/* ---- the victory screen (fighting-game style, AC1Recomp's) ----
 * Black bars, a band in the belt's colour with the belt pattern in it, the
 * beaten AC on its back on the right (dark, smoking), the winner large on
 * the left rising out of the band. The beaten AC slides in from the left
 * (0-0.40 s), the winner from the right (0.30-0.72 s), YOU WIN stamps in
 * with a white flash at 0.72 s (the sting's big chord), then the two lines
 * are typed a word at a time, a tick each. */
#define VIC_DOWN    0.40f
#define VIC_WIN_AT  0.30f
#define VIC_WIN_LEN 0.42f
#define VIC_HIT     0.72f
#define VIC_POP     0.16f
#define VIC_FLASH   0.48f
#define VIC_TYPE    1.95f
#define VIC_WORD    0.18f

/* Victory lines by belt (AC1Recomp's, ladder words fitted to 19 + 1): the
 * words grow with the climb; '#' is the ranks left to the top. */
#define VL_N 8
static const char *const k_victory_belt[11][VL_N] = {
    { "EVERYONE STARTS AT THE BOTTOM.", "NOT BAD FOR A NEWCOMER.", "THE CROWD BARELY NOTICED. YET.",
      "A FIRST STEP. THERE ARE MANY MORE.", "THE BOOKIES JUST CHANGED YOUR ODDS.", "THE LOW RANKS ARE CROWDED. MAKE ROOM.",
      "YOUR AC HAS A FEW SCRATCHES NOW. GOOD.", "THE ARENA KEEPS A RECORD OF EVERYTHING." },
    { "THE ARENA IS STARTING TO KNOW YOUR NAME.", "SOMEONE IN THE STANDS IS CHEERING.", "THE ROOKIES ARE WATCHING YOU NOW.",
      "A FEW SPONSORS ARE TAKING NOTES.", "YOU ARE NOT A FLUKE.", "THAT ONE WILL NEED A NEW AC.",
      "ONE STEP CLOSER TO THE TOP.", "THEY NEVER SAW IT COMING." },
    { "YOUR SPONSORS ARE WATCHING.", "THE CROWD WANTS MORE.", "THE BETTING BOARD HAS YOUR NAME ON IT.",
      "THE BOTTOM OF THE BOARD IS BEHIND YOU.", "CLEAN WORK, RAVEN.", "THE MECHANICS ARE TALKING ABOUT YOUR AC.",
      "YOUR NAME IS ON THE BIG SCREEN NOW.", "THEY WILL STUDY THIS FIGHT." },
    { "THE ARENA HAS STOPPED LAUGHING.", "PEOPLE ARE BETTING ON YOU NOW.", "A CONTENDER, NOT A NEWCOMER.",
      "THE UPPER RANKS JUST FELT A CHILL.", "YOUR REPLAYS ARE GETTING VIEWS.", "THAT WAS NOT LUCK. EVERYONE SAW IT.",
      "THE GARAGE CREWS KNOW YOUR AC BY SOUND.", "THEY WILL REMEMBER THIS ONE." },
    { "THE CROWD CHANTS YOUR NAME.", "THE TOP 10 IS IN SIGHT.", "VETERANS ARE CHECKING YOUR RECORD.",
      "A REAL RAVEN NOW.", "THE PRESS WANTS AN INTERVIEW.", "YOU FIGHT LIKE YOU WERE BORN IN AN AC.",
      "THE RANKERS ABOVE YOU ARE NERVOUS.", "ANOTHER NAME CROSSED OFF THE BOARD." },
    { "HALFWAY UP. THE AIR IS GETTING THIN.", "THE TOP HALF BELONGS TO YOU NOW.", "ONLY THE SERIOUS RAVENS ARE LEFT.",
      "SPONSORS ARE FIGHTING OVER YOU.", "KIDS PAINT THEIR TOY ACS LIKE YOURS.", "THAT WAS A VETERAN. NOW IT IS SCRAP.",
      "THE TOP 8 HAS BEEN WARNED.", "THE BOARD MOVED YOUR NAME UP AGAIN." },
    { "THE TOP 8. FEW EVER GET THIS FAR.", "EVERY FIGHT FROM HERE IS A HEADLINE.", "THE ELITE ARE WATCHING YOUR REPLAYS.",
      "NO ONE CALLS YOU A NEWCOMER NOW.", "THE CROWD IS ON ITS FEET.", "THAT AC WAS BUILT TO BEAT YOU. IT FAILED.",
      "THE RANKS ABOVE ARE RUNNING OUT.", "A NAME THE ARENA WILL NOT FORGET." },
    { "THE TOP 6. LEGENDS LIVE UP HERE.", "THE BOOKIES STOPPED TAKING BETS ON THEM.", "EVEN THE CHAMPIONS ARE WATCHING NOW.",
      "EVERY RAVEN HERE HAS A STORY. NOW YOU DO.", "THE ARENA HAS NEVER SEEN A CLIMB LIKE IT.", "THE TOP 4 IS GETTING CLOSE.",
      "THAT WAS A LEGEND. WAS.", "THE CROWD KNOWS EVERY PART OF YOUR AC." },
    { "THE TOP 4. THE ARENA HOLDS ITS BREATH.", "# MORE TO THE TOP.", "ONLY CHAMPIONS ARE LEFT.",
      "EVERY SPONSOR WANTS YOU NOW.", "EVERY STEP NOW IS ARENA HISTORY.", "THE TOP 2 CAN HEAR YOU COMING.",
      "THERE ARE NO EASY FIGHTS UP HERE.", "# LEFT. THEY ARE ALL WATCHING." },
    { "# MORE TO THE TOP.", "THE LAST CLIMB.", "THE CHAMPION IS WATCHING THIS ONE.",
      "ONE OF THE GREATS. ALMOST THE GREATEST.", "THE WHOLE ARENA CAME TO SEE THIS.", "NOTHING LEFT ABOVE YOU BUT GIANTS.",
      "THE TOP IS IN SIGHT.", "THE CROWD HAS GONE SILENT." },
    { "NO ONE IS LEFT ABOVE YOU.", "THE ARENA BELONGS TO YOU.", "THEY WILL TELL STORIES ABOUT THIS RUN.",
      "THE TOP OF THE ARENA. YOURS.", "NO ONE IS LEFT ABOVE YOU.", "THE ARENA BELONGS TO YOU.",
      "THEY WILL TELL STORIES ABOUT THIS RUN.", "THE TOP OF THE ARENA. YOURS." },
};
static const char *const k_victory_rank_line[] = {
    "RANK # IS YOURS.", "RANK # TAKEN.", "YOU NOW HOLD RANK #.", "UP TO RANK #.", "RANK #. KEEP CLIMBING.",
};
static const char *const k_victory_ghost[] = {
    "YOUR OLD AC IS SCRAP NOW.", "YOU BEAT YOUR OWN PAST.", "THE LAST RUN IS BURIED.", "THAT AC LOST TO YOU TWICE NOW.",
};

static void fill_n(char *out, size_t cap, const char *tpl, int n) {
    size_t k = 0;
    for (const char *q = tpl; *q && k + 1 < cap; ++q) {
        if (*q != '#') { out[k++] = *q; continue; }
        char num[12];
        SDL_snprintf(num, sizeof num, "%d", n);
        for (const char *d = num; *d && k + 1 < cap; ++d) out[k++] = *d;
    }
    out[k] = 0;
}

static void victory_open(void) {
    rogue_sound_build();
    R.vic_t = 0;
    R.vic_words = 0;
    for (int s = 0; s < SLOT_COUNT; ++s) R.vic_parts[s] = R.fight.parts[1][s];
    SDL_memcpy(R.vic_colours, R.fight.colours[1], 12);
    SDL_snprintf(R.vic_beaten, sizeof R.vic_beaten, "%s  DOWN", R.fight.name[1]);
    const int wins = R.run.wins, rank = rank_now();
    SDL_snprintf(R.vic_rank, sizeof R.vic_rank, "RANK %d", rank);
    SDL_snprintf(R.vic_pay, sizeof R.vic_pay, "+%d P", 1 + R.run.income);
    /* the lines, by belt; a run never repeats a line within a belt */
    const int belt = acr_gen1_rogue_belt(wins);
    int belt_top = rank;
    while (belt_top < SLOTS && acr_gen1_rogue_belt(SLOTS - belt_top - 1) == belt) ++belt_top;
    const int step = belt_top - rank;                    /* 0 = just promoted */
    uint64_t q = R.run.seed ^ ((uint64_t)(belt + 1) * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)R.run.attempt << 32);
    const int base = (int)acr_gen1_rogue_below(&q, VL_N);
    const int nr = (int)(sizeof k_victory_rank_line / sizeof k_victory_rank_line[0]);
    if (rank == 1) SDL_strlcpy(R.vic_line[0], "RANK 1. THE CHAMPION.", sizeof R.vic_line[0]);
    else if (wins == 1) SDL_snprintf(R.vic_line[0], sizeof R.vic_line[0], "FIRST WIN. RANK %d IS YOURS.", rank);
    else if (step == 0 && belt > 0) SDL_snprintf(R.vic_line[0], sizeof R.vic_line[0], "%s BELT. RANK %d.", k_belt_said[belt], rank);
    else fill_n(R.vic_line[0], sizeof R.vic_line[0], k_victory_rank_line[(base + wins) % nr], rank);
    if (R.vic_ghost && rank > 1) {
        const int ng = (int)(sizeof k_victory_ghost / sizeof k_victory_ghost[0]);
        SDL_strlcpy(R.vic_line[1], k_victory_ghost[(base + wins) % ng], sizeof R.vic_line[1]);
    } else if (rank == 2) SDL_strlcpy(R.vic_line[1], "ONE FIGHT LEFT. THE TOP AWAITS.", sizeof R.vic_line[1]);
    else fill_n(R.vic_line[1], sizeof R.vic_line[1], k_victory_belt[belt][(base + step) % VL_N], rank - 1);
    sfx_ui(SFX_WIN_STING);
}

static int victory_word_total(void) {
    int total = 0;
    for (int i = 0; i < 2; ++i)
        for (const char *q = R.vic_line[i]; *q; ++q)
            if (*q != ' ' && (q == R.vic_line[i] || q[-1] == ' ')) ++total;
    return total;
}
static int victory_words(void) {
    if (R.vic_t < VIC_TYPE) return 0;
    const int total = victory_word_total(), n = 1 + (int)((R.vic_t - VIC_TYPE) / VIC_WORD);
    return n < total ? n : total;
}
static int victory_typed(void) { return R.vic_t >= VIC_TYPE && victory_words() >= victory_word_total(); }

static void victory_draw(float hud_w, int elapsed_ms) {
    if (R.vic_t < VIC_DONE) R.vic_t += (float)elapsed_ms / 1000.0f;
    const float t = R.vic_t;
    const int words = victory_words();
    if (words > R.vic_words) { R.vic_words = words; if (t < VIC_DONE) sfx_ui(SFX_WIN_TICK); }
    const int belt = acr_gen1_rogue_belt(R.run.wins);
    const uint8_t *band = k_band[belt < 0 ? 0 : belt > 10 ? 10 : belt];
    const float by0 = 85, by1 = 301;
    /* black, the band with the belt pattern pressed in (the hub's tile) */
    acr_hud_rect(0, 0, hud_w, 480, 0, 0, 0, 255, 1);
    acr_hud_rect(0, by0, hud_w, by1 - by0, band[0], band[1], band[2], 255, 1);
    if (R.tile_tex[belt])
        for (float y = by0; y < by1; y += 96)
            for (float x = 0; x < hud_w; x += 96) {
                const float hh = SDL_min(96.0f, by1 - y);
                acr_hud_sprite(R.tile_tex[belt], 64, 64, 0, 0, 64, 64 * hh / 96, x, y, 96, hh, 0, 0, 0, 40, ACR_HUD_ALPHA);
            }
    acr_hud_rect(0, by0 - 4, hud_w, 2, band[0], band[1], band[2], 255, 1);
    /* the beaten AC: on its back, dark, sliding in from the left; smoke once down */
    const float dw = 260, dh = 150, dx_end = hud_w - 27 - dw, dy = by1 + 15 - dh;
    const float dx = -dw + (dx_end + dw) * ease_out(t / VIC_DOWN);
    if (t >= VIC_DOWN) {
        const float k = t < VIC_DOWN + 0.8f ? (t - VIC_DOWN) / 0.8f : 1.0f;
        for (int i = 0; i < 6; ++i) soft_dot(dx + dw * (0.15f + 0.14f * (float)i), by1 - 8, 34, 60, 30, 0, 70);   /* the scorch */
        for (int i = 0; i < 14; ++i) {
            const float u = SDL_fmodf(t * 0.35f + (float)i / 14.0f, 1.0f);
            const float sxp = dx + 40 + (float)((i * 97) % 170), syp = dy + 40 + (float)((i * 53) % 22) - 10;
            soft_dot(sxp + u * 20, syp - u * 100, 15 + u * 28, 60, 58, 56, (uint8_t)(150 * k * (1 - u)));
        }
    }
    draw_ac(R.vic_parts, (const uint8_t (*)[3])R.vic_colours, dx, dy, dw, dh, 1.0f, 0.6f, 1, 0.41f);
    if (t >= VIC_DOWN) {                                     /* sparks, twinkling */
        const int f = (int)(t * 25.0f);
        for (int i = 0; i < 8; ++i) {
            if ((f + i * 3) % 7 >= 3) continue;
            const float spx = dx + 40 + (float)((i * 61) % 180), spy = dy + 30 + (float)((i * 37) % 80);
            soft_dot(spx, spy, 6, 255, 200, 96, 160);
            acr_hud_rect(spx - 4, spy, 9, 1.4f, 255, 240, 180, 255, 1);
            acr_hud_rect(spx, spy - 4, 1.4f, 9, 255, 240, 180, 255, 1);
        }
    }
    /* the winner: large, from the head down, rising out of the band */
    if (t >= VIC_WIN_AT) {
        const float ww = 320, wh = 420, wx_end = SDL_max(20.0f, hud_w * 40 / 853);
        const float wx = hud_w + (wx_end - hud_w) * ease_out((t - VIC_WIN_AT) / VIC_WIN_LEN);
        int me[SLOT_COUNT];
        for (int s = 0; s < SLOT_COUNT; ++s) me[s] = R.run.hangar.fitted[s];
        draw_ac(me, (const uint8_t (*)[3])R.run.hangar.colours, wx, 13, ww, wh, 0.86f, -0.45f, 0, 1.0f);
    }
    /* the bottom bar is in front of both */
    acr_hud_rect(0, by1 + 4, hud_w, 480 - by1 - 4, 0, 0, 0, 255, 1);
    acr_hud_rect(0, by1 + 1, hud_w, 2, band[0], band[1], band[2], 255, 1);
    text_at(24, 22, "ROGUE ARENA", 10, 170, 176, 190);
    if (t >= VIC_HIT) {
        const float sc = t < VIC_HIT + VIC_POP ? 43 : 32;
        text_edged(hud_w * 600 / 1280 - (sc - 32) * 3.5f, 110 - (sc - 32) * 0.5f, "YOU WIN", sc, 240, 240, 240);
        text_right(hud_w - 24, 22, R.vic_rank, 21, 240, 240, 240);
        text_right(hud_w - 24, 52, R.vic_pay, 16, 255, 200, 72);
    }
    if (t >= VIC_HIT + VIC_FLASH) text_right(hud_w - 27, by1 + 16, R.vic_beaten, 10, 170, 176, 190);
    int left = words;
    for (int i = 0; i < 2 && left > 0; ++i) {                /* a word at a time */
        char part[64];
        int k = 0, n = 0;
        const char *q = R.vic_line[i];
        while (q[k] && k < 63) {
            if (q[k] != ' ' && (k == 0 || q[k - 1] == ' ') && ++n > left) break;
            part[k] = q[k];
            ++k;
        }
        while (k > 0 && part[k - 1] == ' ') --k;
        part[k] = 0;
        left -= n > left ? left : n;
        const float sz = text_w(R.vic_line[i], 16) <= hud_w - 93 ? 16 : 11;
        text_at(67, 352 + i * 37, part, sz, 240, 240, 240);
    }
    if (victory_typed() && ((int)(t * 2) & 1) == 0) text_right(hud_w - 24, 456, "ENTER / A  CONTINUE", 10, 170, 176, 190);
    if (t >= VIC_HIT && t < VIC_HIT + VIC_FLASH)            /* the flash */
        acr_hud_rect(0, 0, hud_w, 480, 255, 255, 255, (uint8_t)(230 * (1 - (t - VIC_HIT) / VIC_FLASH)), 1);
}

/* ---- PART SPECS (Triangle / Tab on a prize or a saved part, AC1Recomp's) ----
 * Two windows: the part fitted in that slot now on the left, the new one on
 * the right, the same rows in both, the new values green / red against the
 * old with the difference, the part's shop text from the disc under them,
 * and the AC's weight, legs load and energy before > after. Sets and
 * upgrades get one window saying what they do. Up / down: the next prize. */
enum { ST_WEIGHT, ST_DRAIN, ST_OUTPUT, ST_AP, ST_DEF_S, ST_DEF_E, ST_RADAR, ST_RADAR_R, ST_COMPUTER, ST_ARM_LOAD,
       ST_SLOTS, ST_LOAD, ST_SPEED, ST_TURN, ST_STAB, ST_POWER, ST_BOOST_D, ST_CAP, ST_RED, ST_LOCKS, ST_AREA,
       ST_ATTACK, ST_AMMO, ST_RANGE, ST_RELOAD, ST_EPS, ST_MISS_LOCK, ST_KIND, ST_IMPACT, ST_MISS_RADAR, ST_N };
static const struct { const char *label; int better, key; } k_stat[ST_N] = {
    { "WEIGHT", -1, DS_WEIGHT }, { "ENERGY DRAIN", -1, DS_DRAIN }, { "OUTPUT", 1, DS_OUTPUT }, { "AP", 1, DS_AP },
    { "SHELL DEFENSE", 1, DS_DEF_S }, { "ENERGY DEFENSE", 1, DS_DEF_E }, { "RADAR", 0, DS_RADAR },
    { "RADAR RANGE", 1, DS_RADAR_R }, { "COMPUTER", 0, DS_COMPUTER }, { "ARM LOAD", 1, DS_ARM_LOAD },
    { "OPTION SLOTS", 1, DS_SLOTS }, { "LOAD", 1, DS_LOAD }, { "SPEED", 1, DS_SPEED }, { "TURNING", 1, DS_TURN },
    { "STABILITY", 1, DS_STAB }, { "BOOST POWER", 1, DS_POWER }, { "BOOST DRAIN", -1, DS_BOOST_D },
    { "CAPACITY", 1, DS_CAP }, { "RED ZONE", -1, DS_RED }, { "LOCK TARGETS", 1, DS_LOCKS }, { "LOCK AREA", 0, DS_AREA },
    { "ATTACK", 1, DS_ATTACK }, { "AMMO", 1, DS_AMMO }, { "RANGE", 1, DS_RANGE }, { "RELOAD TIME", -1, DS_RELOAD },
    { "ENERGY PER SHOT", -1, DS_EPS }, { "MISSILE LOCKS", 1, DS_SALVO }, { "AMMO TYPE", 0, DS_AMMO_FLAGS },
    { "IMPACT", 1, DS_IMPACT }, { "MISSILES ON RADAR", 0, DS_RADAR_FLAGS },
};

static int is_radar(int cat, int i) { return cat == ACR_GEN1_CAT_BACK && i >= 0 && i < ACR_GEN1_PART_MAX && (R.info.flags[cat][i] & ACR_GEN1_ROGUE_RADAR); }
static int is_warms(int i) { return i >= 0 && i < ACR_GEN1_PART_MAX && (R.cat.part[ACR_GEN1_CAT_ARMS][i].flags & ACR_GEN1_FIT_ARM_WEAPON); }

static int stat_rows(int cat, int warms, int radar, int out[ST_N]) {
    int n = 0;
    out[n++] = ST_WEIGHT;
    out[n++] = cat == ACR_GEN1_CAT_GENERATOR ? ST_OUTPUT : ST_DRAIN;
    switch (cat) {
    case ACR_GEN1_CAT_HEAD: out[n++] = ST_AP; out[n++] = ST_DEF_S; out[n++] = ST_DEF_E; out[n++] = ST_RADAR;
                            out[n++] = ST_RADAR_R; out[n++] = ST_COMPUTER; break;
    case ACR_GEN1_CAT_CORE: out[n++] = ST_AP; out[n++] = ST_DEF_S; out[n++] = ST_DEF_E; out[n++] = ST_ARM_LOAD;
                            out[n++] = ST_SLOTS; break;
    case ACR_GEN1_CAT_LEGS: out[n++] = ST_AP; out[n++] = ST_DEF_S; out[n++] = ST_DEF_E; out[n++] = ST_LOAD;
                            out[n++] = ST_SPEED; out[n++] = ST_TURN; out[n++] = ST_STAB; break;
    case ACR_GEN1_CAT_ARMS: out[n++] = ST_AP; out[n++] = ST_DEF_S; out[n++] = ST_DEF_E;
                            if (warms) { out[n++] = ST_ATTACK; out[n++] = ST_AMMO; out[n++] = ST_RANGE; out[n++] = ST_RELOAD; out[n++] = ST_KIND; }
                            break;
    case ACR_GEN1_CAT_HAND: case ACR_GEN1_CAT_BACK:
        if (radar) { out[n++] = ST_RADAR_R; out[n++] = ST_MISS_RADAR; break; }
        out[n++] = ST_ATTACK; out[n++] = ST_AMMO; out[n++] = ST_RANGE; out[n++] = ST_RELOAD;
        out[n++] = ST_EPS; out[n++] = ST_MISS_LOCK; out[n++] = ST_KIND; break;
    case ACR_GEN1_CAT_BLADE: out[n++] = ST_ATTACK; out[n++] = ST_IMPACT; break;
    case ACR_GEN1_CAT_BOOSTER: out[n++] = ST_POWER; out[n++] = ST_BOOST_D; break;
    case ACR_GEN1_CAT_FCS: out[n++] = ST_LOCKS; out[n++] = ST_AREA; break;
    case ACR_GEN1_CAT_GENERATOR: out[n++] = ST_CAP; out[n++] = ST_RED; break;
    }
    return n;
}

/* Stat `st` of part (cat, i): 1 and the number, 2 and text, 0 none. */
static int stat_of(int cat, int i, int st, long *v, char *txt, size_t cap) {
    if (!R.specs || cat < 0 || cat >= ACR_GEN1_CAT_COUNT || i < 0 || i >= ACR_GEN1_PART_MAX || !R.specs->present[cat][i]) return 0;
    const int32_t *p = R.specs->v[cat][i];
    const int32_t x = p[k_stat[st].key];
    if (x == DEMO_SPEC_NONE) return 0;
    switch (st) {
    case ST_RADAR: SDL_strlcpy(txt, x ? "YES" : "NO", cap); return 2;
    case ST_RADAR_R: *v = x / 10; return 1;
    case ST_COMPUTER: { static const char *const k[3] = { "STANDARD", "DETAILED", "ROUGH" }; SDL_strlcpy(txt, x >= 0 && x < 3 ? k[x] : "?", cap); return 2; }
    case ST_AREA: { static const char *const k[5] = { "STANDARD", "SIDEWAY", "LENGTHWAY", "WIDE & SHALLOW", "NARROW & DEEP" };
                    SDL_strlcpy(txt, x >= 0 && x < 5 ? k[x] : "?", cap); return 2; }
    case ST_KIND: SDL_strlcpy(txt, x & 0x20 ? "ENERGY" : "SOLID", cap); return 2;
    case ST_MISS_RADAR: SDL_strlcpy(txt, x & 1 ? "SHOWN" : "NOT SHOWN", cap); return 2;
    case ST_MISS_LOCK: if (p[DS_KIND] != 1) return 0; *v = x; return 1;
    case ST_EPS: if (!x) return 0; *v = x; return 1;
    default: *v = x; return 1;
    }
}

static void num(long v, char *out, size_t cap) {
    char t[24];
    SDL_snprintf(t, sizeof t, "%ld", v < 0 ? -v : v);
    const size_t n = SDL_strlen(t);
    size_t k = 0;
    if (v < 0 && k + 1 < cap) out[k++] = '-';
    for (size_t i = 0; i < n && k + 1 < cap; ++i) {
        out[k++] = t[i];
        if ((n - 1 - i) % 3u == 0 && i + 1 < n && k + 1 < cap) out[k++] = ',';
    }
    out[k] = 0;
}

/* The AC's weight, legs load and energy use with these parts. */
static void totals_of(const uint8_t fitted[SLOT_COUNT], long *weight, int *legs_pct, int *energy_pct) {
    acr_gen1_hangar h = R.run.hangar;
    SDL_memcpy(h.fitted, fitted, sizeof h.fitted);
    acr_gen1_fit_build b;
    acr_gen1_garage_build(&h, &R.cat, &b);
    acr_gen1_fit_numbers nm;
    SDL_zero(nm);
    acr_gen1_fit_check(&b, &nm);
    *weight = (long)nm.carried_weight + (b.parts.legs.present ? b.parts.legs.weight : 0);
    *legs_pct = b.parts.legs.carry_limit ? (int)(nm.carried_weight * 100 / b.parts.legs.carry_limit) : 0;
    const long out = b.parts.generator.present ? b.parts.generator.energy_drain : 0;
    *energy_pct = out ? (int)((out - nm.energy_surplus) * 100 / out) : 0;
}

static void specs_draw(float hud_w) {
    acr_hud_rect(0, 0, hud_w, 480, 0, 0, 0, 170, 1);           /* the screen behind, greyed */
    const int from_prize = R.specs_back == RS_PRIZE;
    acr_gen1_rogue_offer o;
    SDL_zero(o);
    char head_r[32];
    if (from_prize) {
        if (R.cur >= R.offers) return;
        o = R.offer[R.cur];
        SDL_snprintf(head_r, sizeof head_r, "PRIZE %d OF %d", R.cur + 1, R.offers);
    } else {
        const acr_gen1_rogue_world w = world();
        if (R.cur >= R.run.saved_n) return;
        if (!acr_gen1_rogue_saved_fit(&R.run, &w, R.cur, &o)) {
            o.kind = ACR_GEN1_OFFER_PART;
            o.cat = R.run.saved[R.cur][0];
            o.index = R.run.saved[R.cur][1];
            o.nput = 0;
        }
        SDL_snprintf(head_r, sizeof head_r, "SAVED %d OF %d", R.cur + 1, R.run.saved_n);
    }
    char line[160];
    const float x0 = SDL_max(12.0f, hud_w * 0.5f - 410), wx = hud_w - 2 * x0;
    panel(x0, 14, wx, 452);
    text_at(x0 + 12, 22, "PART SPECS", 16, 255, 220, 120);
    if (o.kind != ACR_GEN1_OFFER_PART) {                        /* one window: what it does */
        char name[40], cat[96];
        offer_title(&o, name, sizeof name, cat, sizeof cat);
        text_right(x0 + wx - 12, 26, head_r, 8, 150, 156, 170);
        text_at(x0 + 24, 60, name, 16, 255, 255, 255);
        SDL_snprintf(line, sizeof line, "%u P", (unsigned)o.cost);
        text_right(x0 + wx - 24, 60, line, 16, 255, 196, 72);
        float y = 92;
        if (o.kind == ACR_GEN1_OFFER_OPTS) {
            static const char *const k_short[12] = { "MISSILES ON RADAR", "JAMS MISSILE LOCKS", "AUTO MISSILE FIRE",
                "LESS IMPACT SHOCK", "LESS CANNON RECOIL", "MORE ENERGY OUTPUT", "FASTER LOCK-ON", "LESS SOLID DAMAGE",
                "LESS ENERGY DAMAGE", "FASTER ENERGY RECHARGE", "STRONGER ENERGY WEAPONS", "LESS ENERGY USED" };
            for (int i = 0; i < 12; ++i) {
                SDL_snprintf(line, sizeof line, "%-10s %s", R.names && R.names[ACR_GEN1_CAT_OPTION][i][0] ? R.names[ACR_GEN1_CAT_OPTION][i] : "OPTION", k_short[i]);
                text_at(x0 + 24 + (i & 1) * (wx * 0.5f), y + (float)(i / 2) * 16, line, 8, 220, 230, 235);
            }
            y += 6 * 16 + 10;
            text_at(x0 + 24, y, "ALL FITTED: OPTION SLOTS NO LONGER COUNT", 8, 120, 230, 140); y += 16;
            text_at(x0 + 24, y, "FOR THE REST OF THIS ATTEMPT (A RETRY TAKES IT AWAY)", 8, 150, 156, 170);
        } else if (o.kind == ACR_GEN1_OFFER_HPLUS) {
            static const char *const k_hp[] = { "NO LOAD LIMITS: LEGS AND ARMS CARRY ANY WEIGHT",
                "NO ENERGY LIMIT: ANY GENERATOR RUNS ANY PARTS", "BUILT-IN RADAR WHEN NO RADAR IS FITTED",
                "TWO-LEGGED ACS FIRE BACK CANNONS STANDING", "BOOSTING DRAINS HALF, ENERGY REFILLS TWICE AS FAST",
                "FOR THE REST OF THIS ATTEMPT (A RETRY TAKES IT AWAY)" };
            for (int i = 0; i < 6; ++i, y += 18) text_at(x0 + 24, y, k_hp[i], 8, i == 5 ? 150 : 220, i == 5 ? 156 : 230, i == 5 ? 170 : 235);
        } else if (o.kind == ACR_GEN1_OFFER_INCOME) {
            const int now_p = 1 + R.run.income;
            text_at(x0 + 24, y, "EVERY WIN PAYS 1 P MORE FROM NOW ON", 8, 220, 230, 235); y += 18;
            SDL_snprintf(line, sizeof line, "A WIN PAYS %d P NOW, %d P WITH THIS", now_p, now_p + 1);
            text_at(x0 + 24, y, line, 8, 220, 230, 235); y += 18;
            SDL_snprintf(line, sizeof line, "PAYS ITS %u P BACK AFTER %u WINS", (unsigned)o.cost, (unsigned)o.cost);
            text_at(x0 + 24, y, line, 8, 220, 230, 235);
        } else {                                                /* a set: each part, a line of its shop text */
            for (int k = 0; k <= o.xn && k < 3; ++k) {
                const int c = k ? o.xcat[k - 1] : o.cat, id = k ? o.xindex[k - 1] : o.index;
                SDL_snprintf(line, sizeof line, "%s  %s", pname(c, id), cat_label(c));
                text_at(x0 + 24, y, line, 8, 255, 220, 120); y += 14;
                if (R.specs && c < ACR_GEN1_CAT_COUNT && id < ACR_GEN1_PART_MAX)
                    for (int j = 0; j < R.specs->text_n[c][id]; ++j, y += 12) text_at(x0 + 36, y, R.specs->text[c][id][j], 8, 170, 176, 190);
                y += 8;
            }
            if (o.nput && o.put_lost[0]) {
                char lost[80];
                lost_text(&o, 0, lost, sizeof lost);
                SDL_snprintf(line, sizeof line, "REPLACES %s (GONE FOR GOOD)", lost);
                text_at(x0 + 24, y + 6, line, 8, 255, 90, 70);
            }
        }
    } else {                                                    /* two windows */
        const int slot = o.nput ? o.put_slot[0] : (o.cat == ACR_GEN1_CAT_BACK ? ACR_GEN1_SLOT_BACK_L : -1);
        int lslot = slot;
        if (lslot < 0) for (int s = 0; s < SLOT_COUNT; ++s) if (acr_gen1_slot_category((acr_gen1_slot)s) == o.cat) { lslot = s; break; }
        const int lid = lslot >= 0 ? R.run.hangar.fitted[lslot] : ACR_GEN1_NO_PART;
        const int has_l = lid != ACR_GEN1_NO_PART && R.specs && R.specs->present[o.cat][lid];
        const float cw = (wx - 36) * 0.5f, lx = x0 + 12, rx = x0 + 24 + cw, top = 50;
        panel(lx, top, cw, 250);
        panel(rx, top, cw, 250);
        text_at(lx + 10, top + 8, o.nput >= 2 ? "FITTED NOW  LEFT" : "FITTED NOW", 8, 150, 156, 170);
        text_at(rx + 10, top + 8, head_r, 8, 150, 156, 170);
        char tl[48], tr[48];
        SDL_snprintf(tl, sizeof tl, "%s  %s", cat_label(o.cat), has_l ? R.specs->type[o.cat][lid] : "");
        SDL_snprintf(tr, sizeof tr, "%s  %s", cat_label(o.cat), R.specs && R.specs->present[o.cat][o.index] ? R.specs->type[o.cat][o.index] : "");
        text_at(lx + 10, top + 22, has_l ? pname(o.cat, lid) : "(EMPTY)", 16, has_l ? 255 : 150, has_l ? 255 : 156, has_l ? 255 : 170);
        text_at(rx + 10, top + 22, pname(o.cat, o.index), 16, 255, 255, 255);
        text_at(lx + 10, top + 42, tl, 8, 150, 156, 170);
        text_at(rx + 10, top + 42, tr, 8, 150, 156, 170);
        SDL_snprintf(line, sizeof line, "%u P", (unsigned)o.cost);
        if (from_prize) text_right(rx + cw - 10, top + 22, line, 16, 255, 196, 72);
        int rows[ST_N];
        const int n = stat_rows(o.cat, o.cat == ACR_GEN1_CAT_ARMS && (is_warms(o.index) || (has_l && is_warms(lid))), is_radar(o.cat, o.index), rows);
        float y = top + 64;
        for (int i = 0; i < n; ++i) {
            long a = 0, b = 0;
            char ta[24] = "", tb[24] = "";
            const int ka = has_l ? stat_of(o.cat, lid, rows[i], &a, ta, sizeof ta) : 0;
            const int kb = stat_of(o.cat, o.index, rows[i], &b, tb, sizeof tb);
            if (!ka && !kb) continue;
            char va[24], vb[24];
            if (ka == 1) num(a, va, sizeof va); else SDL_strlcpy(va, ka ? ta : "-", sizeof va);
            if (kb == 1) num(b, vb, sizeof vb); else SDL_strlcpy(vb, kb ? tb : "-", sizeof vb);
            text_at(lx + 10, y, k_stat[rows[i]].label, 8, 170, 176, 190);
            text_right(lx + cw - 12, y, va, 8, 220, 230, 235);
            text_at(rx + 10, y, k_stat[rows[i]].label, 8, 170, 176, 190);
            int better = 0;
            char d[24] = "";
            if (ka == 1 && kb == 1 && a != b) {
                char dd[20];
                num(b - a, dd, sizeof dd);
                SDL_snprintf(d, sizeof d, "%s%s", b > a ? "+" : "", dd);
                better = k_stat[rows[i]].better ? ((b > a) == (k_stat[rows[i]].better > 0) ? 1 : -1) : 0;
            }
            const uint8_t cr = better > 0 ? 120 : better < 0 ? 255 : 220, cg = better > 0 ? 230 : better < 0 ? 90 : 230,
                          cb = better > 0 ? 140 : better < 0 ? 70 : 235;
            text_right(rx + cw - 12 - (d[0] ? 64 : 0), y, vb, 8, cr, cg, cb);
            if (d[0]) text_right(rx + cw - 12, y, d, 8, cr, cg, cb);
            y += 15;
        }
        /* the shop's text, under the new part */
        float ty = top + 262;
        if (R.specs && R.specs->present[o.cat][o.index])
            for (int j = 0; j < R.specs->text_n[o.cat][o.index]; ++j, ty += 13)
                text_at(rx, ty, R.specs->text[o.cat][o.index][j], 8, 220, 230, 235);
        if (has_l)
            for (int j = 0, yy = (int)(top + 262); j < R.specs->text_n[o.cat][lid]; ++j, yy += 13)
                text_at(lx, (float)yy, R.specs->text[o.cat][lid][j], 8, 150, 156, 170);
        /* the AC with it fitted, against the AC now */
        if (o.nput) {
            long w0, w1;
            int l0, l1, e0, e1;
            totals_of(R.run.hangar.fitted, &w0, &l0, &e0);
            totals_of(o.put_fitted[0], &w1, &l1, &e1);
            char a[16], b[16];
            num(w0, a, sizeof a);
            num(w1, b, sizeof b);
            SDL_snprintf(line, sizeof line, "YOUR AC:  WEIGHT %s > %s   LEGS CARRY %d%% > %d%%   ENERGY USED %d%% > %d%%%s",
                         a, b, l0, l1, e0, e1, R.run.plus ? "  (HUMAN PLUS)" : "");
            const int bad = !R.run.plus && (l1 > 100 || e1 > 100);
            text_at(x0 + 12, 372, line, 8, bad ? 255 : 220, bad ? 90 : 230, bad ? 70 : 235);
            char lost[80];
            lost_text(&o, 0, lost, sizeof lost);
            if (lost[0]) {
                SDL_snprintf(line, sizeof line, "REPLACES: %s (GONE FOR GOOD)", lost);
                text_at(x0 + 12, 388, line, 8, 150, 156, 170);
            }
        } else if (!from_prize) text_at(x0 + 12, 372, "IT CAN'T BE EQUIPPED WITH YOUR CURRENT PARTS.", 8, 255, 90, 70);
    }
    text_at(x0 + 12, 446, "UP / DOWN  NEXT    TRIANGLE / TAB  OR  ESC / B  CLOSE", 8, 120, 135, 145);
}

void rogue_draw(float hud_w, int elapsed_ms) {
    char line[128], title[64];
    if (R.message_ms > 0) R.message_ms -= elapsed_ms;
    const int belt = acr_gen1_rogue_belt(R.run.wins);
    background(hud_w, R.live || R.borrow ? belt : 0);
    const float lx = 24, cw = hud_w - 48;

    switch (R.screen) {
    case RS_MODE:
        header(hud_w, "ROGUE ARENA");
        panel(lx, 40, cw, 300);
        if (!R.ok) {
            acr_hud_text(lx + 16, 60, "THIS IMPORT HAS NO ARENA OPPONENTS YET.", BAD);
            acr_hud_text(lx + 16, 80, "IMPORT YOUR MASTER OF ARENA DISC AGAIN TO PLAY ROGUE ARENA.", WHITE);
            acr_hud_text(lx + 16, 100, "(DROP THE .CUE ON THE WINDOW, OR RUN ACR-IMPORT.)", DIM);
            break;
        }
        {
            static const char *const k_mode[3] = { "SOLO", "CO-OP (2 PLAYERS)", "BACK" };
            static const char *const k_mode_said[3] = {
                "ONE AC. ONE LIFE. 19 RIVALS.",
                "TWO PLAYERS, ONE LADDER: 19 PAIRS OF RIVALS, 2 AGAINST 2.",
                "BACK TO THE GARAGE." };
            for (int i = 0; i < 3; ++i) row(lx + 24, 70 + i * 22.0f, 260, k_mode[i], i == R.cur, 0);
            acr_hud_text(lx + 16, 150, k_mode_said[R.cur < 3 ? R.cur : 0], WHITE);
            if (R.cur == 1) {
                acr_hud_text(lx + 16, 170, "ON THIS PC: PLAYER 1 ON THE KEYBOARD OR CONTROLLER 1, PLAYER 2 ON", DIM);
                acr_hud_text(lx + 16, 184, "CONTROLLER 2, SPLIT SCREEN. ONLINE CO-OP COMES LATER.", DIM);
            }
        }
        acr_hud_text(lx + 16, 320, "UP / DOWN  CHOOSE    ENTER / A  OK    ESC / B  BACK", DIM);
        break;
    case RS_COOP:
        coop_draw(hud_w, elapsed_ms);
        break;
    case RS_MENU: {
        header(hud_w, "ROGUE ARENA");
        panel(lx, 40, cw, 300);
        if (!R.ok) {
            acr_hud_text(lx + 16, 60, "THIS IMPORT HAS NO ARENA OPPONENTS YET.", BAD);
            acr_hud_text(lx + 16, 80, "IMPORT YOUR MASTER OF ARENA DISC AGAIN TO PLAY ROGUE ARENA.", WHITE);
            acr_hud_text(lx + 16, 100, "(DROP THE .CUE ON THE WINDOW, OR RUN ACR-IMPORT.)", DIM);
            break;
        }
        acr_hud_text(lx + 16, 56, "ONE AC. ONE LIFE. 19 RIVALS.", WHITE);
        acr_hud_text(lx + 16, 72, "WIN FOR P, PICK A PRIZE AFTER EVERY WIN. ONE LOSS ENDS THE RUN.", DIM);
        int ids[4];
        const int n = menu_rows(ids);
        if (R.cur >= n) R.cur = 0;
        for (int i = 0; i < n; ++i) row(lx + 24, 110 + i * 22.0f, 260, k_menu_name[ids[i]], i == R.cur, 0);
        if (R.live) {
            const char *state = R.run.cleared ? "CLEARED" : R.run.over ? "LOST" : "IN PROGRESS";
            SDL_snprintf(line, sizeof line, "LAST RUN: %s  RANK %d  BEST %d  ATTEMPT %d  GHOSTS %d", state, rank_now(),
                         SLOTS - R.run.best, R.run.attempt + 1, R.run.ghosts);
            acr_hud_text(lx + 16, 230, line, WHITE);
            SDL_snprintf(line, sizeof line, "SEED %016llX", (unsigned long long)R.run.seed);
            acr_hud_text(lx + 16, 246, line, DIM);
        }
        acr_hud_text(lx + 16, 320, "UP / DOWN  CHOOSE    ENTER / A  OK    ESC / B  BACK", DIM);
        break;
    }
    case RS_HUB: case RS_STAGES: case RS_SWAP: case RS_SWAP_ASK:
    hub_screens: {
        SDL_snprintf(title, sizeof title, "ROGUE ARENA  RANK %d  %s BELT", rank_now(), acr_gen1_rogue_belt_name(belt));
        header(hud_w, title);
        panel(lx, 40, PANEL_W, 230);
        static const char *const k_rows[HR_COUNT] = { "NEXT BATTLE", "SWAP PARTS", "STAGE", "GARAGE", "QUIT" };
        for (int i = 0; i < HR_COUNT; ++i) {
            const int sel = R.screen == RS_HUB && i == R.cur;
            row(lx + 16, 56 + i * 24.0f, PANEL_W - 16, k_rows[i], sel, R.screen != RS_HUB);
            if (i == HR_SWAP) {
                SDL_snprintf(line, sizeof line, R.run.saved_n ? "%d SAVED" : "NONE", R.run.saved_n);
                acr_hud_text(lx + 170, 56 + i * 24.0f, line, DIM);
            }
            if (i == HR_STAGE) acr_hud_text(lx + 130, 56 + i * 24.0f, stage_name(stage_now()), DIM);
        }
        foe_line(line, sizeof line);
        acr_hud_text(lx + 16, 190, line, WHITE);
        SDL_snprintf(line, sizeof line, "WINS %d  BEST RANK %d  ATTEMPT %d", R.run.wins, SLOTS - R.run.best, R.run.attempt + 1);
        acr_hud_text(lx + 16, 206, line, DIM);
        if (R.run.plus || R.run.opts || R.run.income) {
            SDL_snprintf(line, sizeof line, "%s%s%s", R.run.plus ? "HUMAN PLUS  " : "", R.run.opts ? "ALL OPTIONS  " : "",
                         R.run.income ? "+P PER WIN" : "");
            acr_hud_text(lx + 16, 222, line, GOOD);
        }
        /* The ladder, top to bottom. */
        const float rx = hud_w - 24 - 280;
        panel(rx, 40, 280, 22 + SLOTS * 18.0f);
        for (int i = SLOTS - 1; i >= 0; --i) {
            const float y = 50 + (SLOTS - 1 - i) * 18.0f;
            const int rank = SLOTS - i;
            char n[48] = "";
            if (i == R.run.wins) SDL_snprintf(n, sizeof n, "%s (YOU)", R.run.hangar.ac_name);
            else if (i == 0) SDL_snprintf(n, sizeof n, "-");
            else {
                const acr_gen1_rogue_world w = world();
                acr_gen1_rogue_foe f;
                if (acr_gen1_rogue_rung(&R.run, &w, &R.run.slot[i], &f)) SDL_snprintf(n, sizeof n, "%s%s", f.ac[0] ? f.ac : f.pilot,
                                                                                         R.run.slot[i].kind == ACR_GEN1_ROGUE_GHOST ? " (GHOST)" : "");
            }
            SDL_snprintf(line, sizeof line, "%2d  %s", rank, n);
            if (i == R.run.wins) acr_hud_text(rx + 12, y, line, HILITE);
            else if (i < R.run.wins) acr_hud_text(rx + 12, y, line, DIM);
            else if (i == R.run.wins + 1) acr_hud_text(rx + 12, y, line, BAD);
            else acr_hud_text(rx + 12, y, line, WHITE);
        }
        if (R.screen == RS_STAGES) {
            panel(lx + 40, 120, PANEL_W, 40 + R.nstages * 20.0f);
            acr_hud_text(lx + 56, 130, "STAGE: WHERE YOUR NEXT FIGHTS TAKE PLACE", HILITE);
            for (int i = 0; i < R.nstages; ++i)
                row(lx + 64, 150 + i * 20.0f, PANEL_W - 40, k_stage[R.stages[i]].name, i == R.cur, 0);
            acr_hud_text(lx + 56, 150 + R.nstages * 20.0f, "Q / LB  RANDOM", DIM);
        }
        if (R.screen == RS_SWAP || R.screen == RS_SWAP_ASK || R.screen == RS_SPECS) {
            const acr_gen1_rogue_world w = world();
            const float h = 70 + (R.run.saved_n + 1) * 20.0f;
            panel(lx + 40, 100, cw - 360, h);
            acr_hud_text(lx + 56, 110, "SWAP PARTS: WHAT A SAVED PART REPLACES IS GONE FOR GOOD", HILITE);
            acr_hud_text(lx + 56, 100 + h + 6, "TAB / TRIANGLE  PART SPECS", DIM);
            for (int i = 0; i <= R.run.saved_n; ++i) {
                if (i == R.run.saved_n) { row(lx + 64, 130 + i * 20.0f, 200, "BACK", R.cur == i, 0); break; }
                acr_gen1_rogue_offer o;
                const int fits = acr_gen1_rogue_saved_fit(&R.run, &w, i, &o);
                SDL_snprintf(line, sizeof line, "%-18s %s", pname(R.run.saved[i][0], R.run.saved[i][1]), cat_label(R.run.saved[i][0]));
                row(lx + 64, 130 + i * 20.0f, cw - 400, line, R.cur == i, !fits);
                if (R.cur == i && fits) {
                    char lost[80];
                    lost_text(&o, 0, lost, sizeof lost);
                    SDL_snprintf(line, sizeof line, "REPLACES: %s", lost[0] ? lost : "NOTHING");
                    acr_hud_text(lx + 56, 100 + h - 18, line, DIM);
                }
            }
            if (R.screen == RS_SWAP_ASK) {
                acr_gen1_rogue_offer o;
                acr_gen1_rogue_saved_fit(&R.run, &w, R.cur, &o);
                panel(lx + 260, 160, 220, 20 + R.ask_n * 20.0f);
                for (int k = 0; k < R.ask_n; ++k)
                    row(lx + 280, 170 + k * 20.0f, 190, R.ask_put[k] < 0 ? "BACK" : put_name(&o, R.ask_put[k]), k == R.ask_cur, 0);
            }
        }
        break;
    }
    case RS_VS:
        vs_draw(hud_w, elapsed_ms);
        break;
    case RS_RESULT:
        victory_draw(hud_w, elapsed_ms);
        break;
    case RS_SPECS:
    case RS_PRIZE: case RS_PRIZE_ASK: case RS_REROLL_ASK: {
        if (R.screen == RS_SPECS && R.specs_back == RS_SWAP) goto hub_screens;
        if (R.borrow) SDL_snprintf(title, sizeof title, "%s PRIZE  RANK %d", R.whose, rank_now());
        else SDL_snprintf(title, sizeof title, "PRIZE  RANK %d", rank_now());
        header(hud_w, title);
        static const char *const k_tier[3] = { "CHEAP", "MID", "EXPENSIVE" };
        panel(lx, 40, cw, 300);
        acr_hud_text(lx + 16, 52, "PICK ONE PRIZE WITH P, OR PASS FOR +1 P.", WHITE);
        for (int i = 0; i < R.offers; ++i) {
            const acr_gen1_rogue_offer *o = &R.offer[i];
            char name[40], cat[96];
            offer_title(o, name, sizeof name, cat, sizeof cat);
            const float y = 80 + i * 56.0f;
            const int poor = (int32_t)o->cost > R.run.hangar.credits;
            if (i == R.cur && R.screen == RS_PRIZE) acr_hud_rect(lx + 6, y - 6, cw - 12, 50, 60, 70, 40, 255, 1);
            SDL_snprintf(line, sizeof line, "%-9s %-20s %2u P", k_tier[o->band < 3 ? o->band : 2], name, (unsigned)o->cost);
            acr_hud_text(lx + 16, y, line, poor ? 120 : 255, poor ? 135 : 220, poor ? 145 : 120);
            acr_hud_text(lx + 100, y + 14, cat, DIM);
            if (i == R.cur) part_lines(lx + 420, y, o);
        }
        if (!R.offers) acr_hud_text(lx + 16, 90, "NOTHING FITS YOUR AC THIS TIME.", DIM);
        const float by = 80 + 3 * 56.0f + 10;
        const int on = R.cur == R.offers && R.screen == RS_PRIZE;
        if (on && !R.alt) acr_hud_rect(lx + 10, by - 3, 140, 14, 60, 70, 40, 255, 1);
        if (on && R.alt) acr_hud_rect(lx + 170, by - 3, 200, 14, 60, 70, 40, 255, 1);
        acr_hud_text(lx + 16, by, "PASS (+1 P)", on && !R.alt ? 255 : 220, on && !R.alt ? 220 : 230, on && !R.alt ? 120 : 235);
        const int rerolled = R.run.reroll_win == R.run.prize_wins + 1u;
        acr_hud_text(lx + 176, by, rerolled ? "RE-ROLLED" : "RE-ROLL (1 P)", rerolled ? 120 : on && R.alt ? 255 : 220,
                     rerolled ? 135 : on && R.alt ? 220 : 230, rerolled ? 145 : on && R.alt ? 120 : 235);
        acr_hud_text(lx + 16, 320, "UP / DOWN  CHOOSE   LEFT / RIGHT  PASS OR RE-ROLL   ENTER / A  OK   TAB / TRIANGLE  SPECS", DIM);
        if (R.screen == RS_PRIZE_ASK) {
            const acr_gen1_rogue_offer *o = &R.offer[R.cur];
            char name[40], cat[96];
            offer_title(o, name, sizeof name, cat, sizeof cat);
            panel(hud_w * 0.5f - 160, 150, 320, 46 + R.ask_n * 20.0f);
            SDL_snprintf(line, sizeof line, "%s  FOR %u P", name, (unsigned)o->cost);
            acr_hud_text(hud_w * 0.5f - 148, 160, line, HILITE);
            for (int k = 0; k < R.ask_n; ++k) {
                const int put = R.ask_put[k];
                const char *t = put == -2 ? "BACK" : put == -1 ? "SAVE FOR LATER"
                              : (o->kind == ACR_GEN1_OFFER_PART || o->kind == ACR_GEN1_OFFER_SET) ? put_name(o, put) : "BUY";
                row(hud_w * 0.5f - 132, 184 + k * 20.0f, 280, t, k == R.ask_cur, 0);
            }
        }
        if (R.screen == RS_REROLL_ASK) {
            panel(hud_w * 0.5f - 200, 160, 400, 86);
            acr_hud_text(hud_w * 0.5f - 188, 170, "RE-ROLL FOR 1 P", HILITE);
            acr_hud_text(hud_w * 0.5f - 188, 184, "3 NEW PRIZES IN PLACE OF THESE. ONE PER WIN.", WHITE);
            row(hud_w * 0.5f - 170, 206, 160, "RE-ROLL", R.ask_cur == 0, 0);
            row(hud_w * 0.5f - 170, 224, 160, "BACK", R.ask_cur == 1, 0);
        }
        break;
    }
    case RS_LOST: {
        header(hud_w, "DEFEATED");
        panel(lx, 120, cw, 200);
        SDL_snprintf(line, sizeof line, "YOUR RUN ENDED AT RANK %d.  BEST RANK %d.", rank_now(), SLOTS - R.run.best);
        acr_hud_text(lx + 30, 140, line, WHITE);
        if (R.run.wins) {
            SDL_snprintf(line, sizeof line, "RETRY: YOUR AC STAYS BEHIND AS A GHOST AT RANK %d.", rank_now());
            acr_hud_text(lx + 30, 156, line, DIM);
        }
        static const char *const k_lost[3] = { "RETRY ARENA", "NEW ARENA", "QUIT" };
        for (int i = 0; i < 3; ++i) row(lx + 40, 190 + i * 22.0f, 260, k_lost[i], i == R.cur, 0);
        acr_hud_text(lx + 30, 280, "RETRY: THE SAME 19 WITH A NEW RANDOM AC AT 0 P.", DIM);
        break;
    }
    case RS_CLEARED: {
        header(hud_w, "ROGUE ARENA CLEARED");
        panel(lx, 120, cw, 200);
        acr_hud_text(lx + 30, 140, "RANK 1.  BLACK-GOLD BELT.", HILITE);
        SDL_snprintf(line, sizeof line, "ATTEMPTS %d   GHOSTS ON THE LADDER %d   SEED %016llX", R.run.attempt + 1, R.run.ghosts,
                     (unsigned long long)R.run.seed);
        acr_hud_text(lx + 30, 160, line, WHITE);
        static const char *const k_done[2] = { "NEW ARENA", "BACK TO THE GARAGE" };
        for (int i = 0; i < 2; ++i) row(lx + 40, 200 + i * 22.0f, 260, k_done[i], i == R.cur, 0);
        break;
    }
    }
    if (R.screen == RS_SPECS) specs_draw(hud_w);
    if (R.message_ms > 0 && R.message[0]) {
        const float w = 8.0f * (float)SDL_strlen(R.message) + 24;
        panel(hud_w * 0.5f - w * 0.5f, 440, w, 22);
        acr_hud_text(hud_w * 0.5f - w * 0.5f + 12, 447, R.message, HILITE);
    }
}

int rogue_vs_due(void) {
    return R.screen == RS_VS && vs_due();
}

/* ---- co-op ---- */

#include "rogue_coop.inc"
