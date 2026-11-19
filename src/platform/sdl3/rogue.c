#include "rogue.h"

#include <SDL3/SDL.h>

#include <string.h>

#include "../../render/acr_render.h"
#include "rogue_tiles_data.h"

#define SLOTS ACR_GEN1_ROGUE_SLOTS

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define GOOD 120, 230, 140
#define BAD 255, 90, 70

/* ---- state ---- */

enum { RS_MENU, RS_HUB, RS_STAGES, RS_SWAP, RS_SWAP_ASK, RS_VS, RS_RESULT, RS_PRIZE, RS_PRIZE_ASK,
       RS_REROLL_ASK, RS_LOST, RS_CLEARED };

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
    int vs_ms;
    int last_won;                             /* the result screen: 1 a win */
    int stages[16], nstages;
    uint32_t tile_tex[11];
} R;

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
    if (!R.live) return;
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
    R.nstages = 0;
    for (int i = 0; i < NSTAGE && R.nstages < 16; ++i) if (stage_exists(k_stage[i].id)) R.stages[R.nstages++] = i;
    R.ok = R.foes >= SLOTS;
    R.live = R.ok && load_run();
    R.screen = RS_MENU;
    return R.ok;
}

static uint64_t fresh_seed(void) {
    const char *env = SDL_getenv("ACR_ROGUE_SEED");
    if (env && env[0]) return acr_gen1_rogue_seed_from_text(env);
    uint64_t s = SDL_GetTicksNS() ^ ((uint64_t)SDL_GetPerformanceCounter() << 17);
    return acr_gen1_rogue_next(&s);
}

static void to_hub(void) {
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

void rogue_open(const char *pilot, const char *ac) {
    SDL_strlcpy(R.pilot, pilot && pilot[0] ? pilot : "RAVEN", sizeof R.pilot);
    SDL_strlcpy(R.ac, ac && ac[0] ? ac : "AC", sizeof R.ac);
    R.screen = RS_MENU;
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
        acr_gen1_rogue_won(&R.run);
        R.last_won = 1;
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

rogue_command rogue_input(garage_action a) {
    if (a == GA_NONE) return RC_NONE;
    const acr_gen1_rogue_world w = world();
    switch (R.screen) {
    case RS_MENU: {
        int ids[4];
        const int n = menu_rows(ids);
        if (!R.ok) { if (a == GA_OK || a == GA_CANCEL) return RC_LEAVE; return RC_NONE; }
        R.cur = step(R.cur, n, a);
        if (R.cur >= n) R.cur = 0;
        if (a == GA_CANCEL) return RC_LEAVE;
        if (a != GA_OK) return RC_NONE;
        switch (ids[R.cur]) {
        case MI_CONTINUE: to_hub(); break;
        case MI_RETRY:
            if (acr_gen1_rogue_retry(&R.run, &w)) { save_run(); to_hub(); }
            break;
        case MI_NEW: new_arena(); break;
        default: return RC_LEAVE;
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
            R.screen = RS_VS;
            R.vs_ms = 5000;
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
            if (acr_gen1_rogue_swap(&R.run, &w, R.cur, R.ask_put[R.ask_cur])) { say("EQUIPPED."); save_run(); }
            R.screen = R.run.saved_n ? RS_SWAP : RS_HUB;
            if (R.cur >= R.run.saved_n) R.cur = R.run.saved_n ? R.run.saved_n - 1 : HR_SWAP;
        }
        return RC_NONE;
    case RS_VS:
        if (a == GA_CANCEL) { R.screen = RS_HUB; R.cur = HR_NEXT; return RC_NONE; }
        if (a == GA_OK) { R.vs_ms = 0; return RC_FIGHT; }
        return RC_NONE;
    case RS_RESULT:
        if (a == GA_OK || a == GA_CANCEL) to_hub();
        return RC_NONE;
    case RS_PRIZE: {
        const int rows = R.offers + 1;
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
    }
    return RC_NONE;
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

void rogue_draw(float hud_w, int elapsed_ms) {
    char line[128], title[64];
    if (R.message_ms > 0) R.message_ms -= elapsed_ms;
    const int belt = acr_gen1_rogue_belt(R.run.wins);
    background(hud_w, R.live ? belt : 0);
    const float lx = 24, cw = hud_w - 48;

    switch (R.screen) {
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
    case RS_HUB: case RS_STAGES: case RS_SWAP: case RS_SWAP_ASK: {
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
        if (R.screen == RS_SWAP || R.screen == RS_SWAP_ASK) {
            const acr_gen1_rogue_world w = world();
            const float h = 70 + (R.run.saved_n + 1) * 20.0f;
            panel(lx + 40, 100, cw - 360, h);
            acr_hud_text(lx + 56, 110, "SWAP PARTS: WHAT A SAVED PART REPLACES IS GONE FOR GOOD", HILITE);
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
    case RS_VS: {
        header(hud_w, "VS");
        R.vs_ms -= elapsed_ms;
        const rogue_fight *g = &R.fight;
        panel(lx, 120, cw, 200);
        acr_hud_text(lx + 30, 150, g->name[0], HILITE);
        SDL_snprintf(line, sizeof line, "RANK %d", rank_now());
        acr_hud_text(lx + 30, 166, line, WHITE);
        acr_hud_text(hud_w * 0.5f - 8, 200, "VS", BAD);
        const float rx2 = lx + cw - 30 - 8.0f * (float)SDL_strlen(g->name[1]);
        acr_hud_text(rx2, 230, g->name[1], HILITE);
        SDL_snprintf(line, sizeof line, "RANK %d%s", rank_now() - 1, next_is_ghost() ? "  GHOST" : "");
        acr_hud_text(lx + cw - 30 - 8.0f * (float)SDL_strlen(line), 246, line, WHITE);
        const int secs = R.vs_ms > 0 ? (R.vs_ms + 999) / 1000 : 0;
        SDL_snprintf(line, sizeof line, "%s   UNITS DEPLOY IN %d", stage_name(g->stage), secs);
        acr_hud_text(hud_w * 0.5f - 4.0f * (float)SDL_strlen(line), 290, line, WHITE);
        acr_hud_text(lx + 16, 340, "ENTER / A  START NOW    ESC / B  BACK TO THE HUB", DIM);
        break;
    }
    case RS_RESULT:
        header(hud_w, "WIN");
        panel(lx, 140, cw, 150);
        SDL_snprintf(line, sizeof line, "YOU ARE NOW RANK %d  (%s BELT)", rank_now(), acr_gen1_rogue_belt_name(belt));
        acr_hud_text(lx + 30, 170, line, HILITE);
        SDL_snprintf(line, sizeof line, "+%d P", 1 + R.run.income);
        acr_hud_text(lx + 30, 190, line, GOOD);
        acr_hud_text(lx + 30, 260, "ENTER / A  CONTINUE", DIM);
        break;
    case RS_PRIZE: case RS_PRIZE_ASK: case RS_REROLL_ASK: {
        SDL_snprintf(title, sizeof title, "PRIZE  RANK %d", rank_now());
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
        acr_hud_text(lx + 16, 320, "UP / DOWN  CHOOSE   LEFT / RIGHT  PASS OR RE-ROLL   ENTER / A  OK", DIM);
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
    if (R.message_ms > 0 && R.message[0]) {
        const float w = 8.0f * (float)SDL_strlen(R.message) + 24;
        panel(hud_w * 0.5f - w * 0.5f, 440, w, 22);
        acr_hud_text(hud_w * 0.5f - w * 0.5f + 12, 447, R.message, HILITE);
    }
}

int rogue_vs_due(void) {
    return R.screen == RS_VS && R.vs_ms <= 0;
}
