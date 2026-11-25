/* Rogue Arena co-op online (docs/ROGUE-ARENA-COOP.md), included at the end
 * of rogue_coop.inc.
 *
 * The host keeps the run (its files are the run) and sends it whole, as
 * text (acr_gen1_coop_state_write, a few KB), whenever it changes. The
 * guest keeps a copy it never saves. Each PC shows only its own player's
 * screens, so both players answer their rejoin choice and prize screen at
 * the same time. What the guest does goes to the host as a message; the
 * host applies it with the same rules and sends the run again.
 *
 * Messages (net_link_msg_*: whole, in order, sent again until they arrive),
 * the first byte says which:
 *   host -> guest  'C' data check (the arena opponents, the stages)
 *                  'P' the host is on the co-op menu (1) or in the run (0)
 *                  'S' the run (text)
 *                  'F' a fight starts: kind (0 the pair, 1 the duel), seed
 *                  'E' the fight's result: result, player 1 down, player 2 down
 *                  'M' a line to show (a refused choice)
 *   guest -> host  'D' the guest's side after its prize screen (text)
 *                  'J' the rejoin choice (1 KEEP, 0 NEW RANDOM AC)
 *                  'H' the stash part the guest highlights (or none)
 *                  'T' take that stash part, into place `put`
 *   both ways      'R' READY: which player, on / off (the host can clear the guest's)
 * Fights run on rollback (main.c, the same link); the pair's pilots fly
 * inside the match on both PCs. The host decides when a fight is over (once
 * every step of it is confirmed) and sends 'E'. */

#include "../../net/acr_msg.h"

static uint8_t g_net_buf[ACR_MSG_MAX];

static void net_send(int type, const void *data, int n) {
    if (!N.link || n < 0 || n + 1 > (int)sizeof g_net_buf) return;
    g_net_buf[0] = (uint8_t)type;
    if (n) memcpy(g_net_buf + 1, data, (size_t)n);
    if (!net_link_msg_send(N.link, g_net_buf, n + 1)) SDL_Log("rogue co-op online: message '%c' dropped (queue full)", type);
}

/* Host: sends the run if it changed. */
static void net_flush(void) {
    if (!N.link || guest() || !N.dirty || !C.live) return;
    static char text[96 * 1024];
    const size_t n = acr_gen1_coop_state_write(&C.run, text, sizeof text);
    if (n >= sizeof text) return;
    net_send('S', text, (int)n + 1);
    N.dirty = 0;
}

static void net_end(const char *why) {
    if (!N.link) return;
    const int was_guest = guest();
    SDL_strlcpy(N.why, why, sizeof N.why);
    SDL_Log("rogue co-op online: the session is over: %s", why);
    N.link = NULL;
    N.closing = 1;
    N.ready[0] = N.ready[1] = 0;
    N.fight_at = 0;
    N.await = 0;
    N.result_due = 0;
    if (R.borrow) { R.run = R.solo; R.borrow = NULL; }   /* off a prize screen (it comes again) */
    R.screen = RS_COOP;
    C.run.hold[0] = C.run.hold[1] = -1;
    if (was_guest) C.live = coop_load();      /* this PC's own (one-PC) run again */
    else coop_save();
    C.screen = CS_MENU;
    C.cur = 0;
}

static void send_ready(int p, int on) {
    N.ready[p] = on;
    const uint8_t b[2] = { (uint8_t)p, (uint8_t)(on != 0) };
    net_send('R', b, 2);
}

static void net_ready(int on) {
    send_ready(N.local, on);
    if (on) R.snd = SFX_UI_EQUIP;
}

/* A stash part as the network names it: category, part, who saved it. */
static void item_id(int n, uint8_t id[3]) {
    const acr_gen1_coop_item *it = &C.run.stash[n];
    id[0] = it->cat; id[1] = it->index; id[2] = it->owner;
}

/* The host's index of the part the guest means (the stash may have moved on). */
static int find_item(const uint8_t id[3]) {
    int found = -1;
    for (int i = 0; i < C.run.stash_n; ++i) {
        const acr_gen1_coop_item *it = &C.run.stash[i];
        if (it->cat != id[0] || it->index != id[1] || it->owner != id[2] || C.run.hold[0] == i) continue;
        if (C.run.hold[1] == i) return i;
        if (found < 0) found = i;
    }
    return found;
}

static void net_hold(int n) {
    if (n >= C.run.stash_n) n = -1;
    if (guest()) {
        uint8_t b[4] = { 0, 0, 0, 0 };
        if (n >= 0) { b[0] = 1; item_id(n, b + 1); }
        net_send('H', b, 4);
    }
    if (!acr_gen1_coop_hold(&C.run, N.local, n)) say("THE OTHER PLAYER IS ON THAT PART.");
    N.dirty = 1;
}

static void net_take(int n, int put) {
    const acr_gen1_rogue_world w = world();
    if (n < 0 || n >= C.run.stash_n) return;
    if (guest()) {
        uint8_t b[4];
        item_id(n, b);
        b[3] = (uint8_t)put;
        net_send('T', b, 4);
        /* Shown at once; the host's run (which follows) has the last word. */
        if (acr_gen1_coop_stash_take(&C.run, &w, 1, n, put)) { say("EQUIPPED."); R.snd = SFX_UI_EQUIP; }
        return;
    }
    if (acr_gen1_coop_stash_take(&C.run, &w, 0, n, put)) {
        say("EQUIPPED.");
        R.snd = SFX_UI_EQUIP;
        coop_save();
    }
}

static void net_say(const char *t) { net_send('M', t, (int)SDL_strlen(t) + 1); }

/* The data both PCs must share for the run: the arena opponents and the stages. */
static void data_check(uint32_t out[1 + NSTAGE]) {
    out[0] = net_tree_check(R.import_dir, "arena");
    for (int i = 0; i < NSTAGE; ++i) out[1 + i] = net_stage_check(R.import_dir, k_stage[i].id);
}

static void put32le(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static uint32_t get32le(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

/* Keeps the cursor on the stash part this player holds after the stash changed. */
static void stash_follow(void) {
    if (C.screen != CS_STASH && C.screen != CS_STASH_ASK) return;
    if (!C.run.stash_n) { C.screen = CS_HUB; C.cur = CH_STASH; return; }
    const int h = C.run.hold[N.local];
    if (h >= 0) C.cur = h;
    else if (C.cur > C.run.stash_n) C.cur = C.run.stash_n;
    if (C.screen == CS_STASH_ASK && C.cur >= C.run.stash_n) C.screen = CS_STASH;
}

/* ---- the host ---- */

static void host_got(const uint8_t *m, int n) {
    const acr_gen1_rogue_world w = world();
    switch (m[0]) {
    case 'D':                                 /* the guest's prize screen is done */
        if (m[n - 1] == 0 && C.live && acr_gen1_coop_side_read(&C.run, 1, (const char *)m + 1)) {
            acr_gen1_coop_collect(&C.run, 1);
            coop_save();
        } else {
            SDL_Log("rogue co-op online: the guest's side was refused");
        }
        N.dirty = 1;
        break;
    case 'J':
        if (n >= 2 && C.live && C.run.rejoin[1]) {
            if (acr_gen1_coop_rejoin(&C.run, &w, 1, m[1] != 0)) coop_save();
            else net_say("NO NEW AC COULD BE MADE.");
        }
        N.dirty = 1;
        break;
    case 'H':
        if (n >= 5) {
            const int i = m[1] ? find_item(m + 2) : -1;
            if (m[1] && i < 0) net_say("THE OTHER PLAYER IS ON THAT PART.");
            acr_gen1_coop_hold(&C.run, 1, i);
        }
        N.dirty = 1;
        break;
    case 'T':
        if (n >= 5) {
            const int i = find_item(m + 1);
            if (i >= 0 && acr_gen1_coop_stash_take(&C.run, &w, 1, i, m[4])) coop_save();
            else net_say("THE OTHER PLAYER IS ON THAT PART.");
        }
        N.dirty = 1;
        break;
    case 'R':
        if (n >= 3 && m[1] == 1) N.ready[1] = m[2] != 0;
        break;
    }
    stash_follow();
}

/* Both READY: the host sets the fight up, sends it and counts down. */
static void host_try_fight(void) {
    if (!N.ready[0] || !N.ready[1] || R.screen != RS_COOP || R.borrow) return;
    if (C.screen != CS_HUB && C.screen != CS_DUEL) return;
    send_ready(0, 0);
    send_ready(1, 0);
    int kind = 0;
    const char *why = NULL;
    if (C.run.duel_due) {
        coop_prepare_duel();
        kind = 1;
    } else if (!acr_gen1_coop_ready(&C.run)) {
        why = "BOTH PLAYERS MUST FINISH THEIR PRIZE SCREENS FIRST.";
    } else if (acr_gen1_rogue_check(&C.run.player[0], &R.cat)) {
        why = "THE HOST'S AC CAN'T SORTIE AS IT IS.";
    } else if (acr_gen1_rogue_check(&C.run.player[1], &R.cat)) {
        why = "PLAYER 2'S AC CAN'T SORTIE AS IT IS.";
    } else if (!R.nstages || !coop_prepare_pair()) {
        why = "THE NEXT PAIR COULD NOT BE LOADED.";
    }
    if (why) { say(why); net_say(why); return; }
    uint64_t s = fresh_seed();
    R.fight.net_seed = (uint32_t)acr_gen1_rogue_next(&s) | 1u;
    net_flush();                              /* the guest builds the fight from the same run */
    uint8_t f[6];
    f[0] = (uint8_t)kind;
    put32le(f + 1, R.fight.net_seed);
    f[5] = C.run.wins;
    net_send('F', f, 6);
    C.screen = CS_VS;
    N.fight_at = N.now + 3000;
    SDL_Log("rogue co-op online: fight (%s) at rank %d, seed %08X", kind ? "the duel" : "the pair", coop_rank(),
            (unsigned)R.fight.net_seed);
}

/* ---- the guest ---- */

static void guest_got(const uint8_t *m, int n) {
    switch (m[0]) {
    case 'C': {
        uint32_t mine[1 + NSTAGE];
        data_check(mine);
        int same = n >= 1 + 4 * (1 + NSTAGE) && R.ok;
        for (int i = 0; same && i < 1 + NSTAGE; ++i) same = get32le(m + 1 + 4 * i) == mine[i];
        if (!same) net_end("THE HOST'S ARENA DATA IS DIFFERENT FROM THIS PC'S.\nIMPORT THE SAME DISC ON BOTH PCS.");
        break;
    }
    case 'P':
        if (n >= 2) {
            N.host_menu = m[1] != 0;
            if (R.screen == RS_COOP && !R.borrow && (C.screen == CS_WAIT || N.host_menu)) coop_next();
        }
        break;
    case 'S': {
        if (m[n - 1] != 0) break;
        static acr_gen1_coop next;
        if (!acr_gen1_coop_state_read(&next, (const char *)m + 1)) { SDL_Log("rogue co-op online: a bad run from the host"); break; }
        /* Our prize screen's result hasn't reached the host yet: keep ours. */
        if (N.side_pending) {
            if (next.player[1].prize_wins < C.run.player[1].prize_wins) next.player[1] = C.run.player[1];
            else N.side_pending = 0;
        }
        C.run = next;
        C.live = 1;
        N.got_state = 1;
        N.state_after_result = 1;
        if (N.await == AW_STATE || (N.await == AW_REJOIN && !C.run.rejoin[1])) N.await = AW_NONE;
        if (R.screen != RS_COOP || R.borrow) break;
        stash_follow();
        if (C.screen == CS_WAIT || C.screen == CS_LOST || C.screen == CS_CLEARED || C.screen == CS_MENU ||
            (C.screen == CS_REJOIN && !C.run.rejoin[1]) ||
            ((C.screen == CS_HUB || C.screen == CS_DUEL) && (C.run.over || C.run.cleared || C.run.rejoin[1] ||
                                                             acr_gen1_coop_prize_due(&C.run, 1))))
            coop_next();
        break;
    }
    case 'R':
        if (n >= 3 && m[1] < 2) N.ready[m[1]] = m[2] != 0;
        break;
    case 'F': {
        if (n < 7) break;
        N.ready[0] = N.ready[1] = 0;
        if (R.borrow) { R.run = R.solo; R.borrow = NULL; }
        R.screen = RS_COOP;
        C.run.hold[0] = C.run.hold[1] = -1;
        if (m[1]) coop_prepare_duel();
        else if (C.run.wins != m[6] || !coop_prepare_pair()) {
            net_end("THE TWO PCS DISAGREE ABOUT THE NEXT FIGHT.");
            break;
        }
        R.fight.net_seed = get32le(m + 2);
        C.screen = CS_VS;
        N.fight_at = N.now + 3000;
        break;
    }
    case 'E':
        if (n >= 4) { N.result_due = 1; N.res = m[1]; N.down[0] = m[2]; N.down[1] = m[3]; N.state_after_result = 0; }
        break;
    case 'M':
        if (m[n - 1] == 0) say((const char *)m + 1);
        break;
    }
}

/* ---- main.c's side ---- */

void rogue_coop_online_begin(net_link *link, const net_match_info *info) {
    SDL_zero(N);
    N.link = link;
    N.local = info->local ? 1 : 0;
    N.delay = info->delay;
    for (int p = 0; p < 2; ++p) {
        SDL_strlcpy(N.pilot[p], info->pilot[p][0] ? info->pilot[p] : info->name[p], sizeof N.pilot[p]);
        SDL_strlcpy(N.ac[p], info->name[p], sizeof N.ac[p]);
        if (!N.pilot[p][0]) SDL_snprintf(N.pilot[p], sizeof N.pilot[p], "PLAYER %d", p + 1);
        if (!N.ac[p][0]) SDL_snprintf(N.ac[p], sizeof N.ac[p], p ? "PARTNER" : "AC");
    }
    if (R.borrow) { R.run = R.solo; R.borrow = NULL; }
    R.screen = RS_COOP;
    C.cur = 0;
    C.last_draw = 0;
    if (!guest()) {
        C.live = coop_load();
        C.run.hold[0] = C.run.hold[1] = -1;
        C.screen = CS_MENU;
        uint32_t chk[1 + NSTAGE];
        uint8_t b[4 * (1 + NSTAGE)];
        data_check(chk);
        for (int i = 0; i < 1 + NSTAGE; ++i) put32le(b + 4 * i, chk[i]);
        net_send('C', b, (int)sizeof b);
        const uint8_t menu = 1;               /* before the run, so the guest waits on it */
        net_send('P', &menu, 1);
        N.sent_menu = 1;
        N.dirty = C.live;
    } else {
        C.live = 0;
        SDL_zero(C.run);
        C.screen = CS_WAIT;
    }
    SDL_Log("rogue co-op online: you are %s (%s), with %s", N.local ? "the guest, player 2" : "the host, player 1",
            N.pilot[N.local], N.pilot[1 - N.local]);
}

int rogue_coop_online(void) { return N.link != NULL; }

int rogue_coop_net_result(int *result, int *p1_down, int *p2_down) {
    if (!N.result_due) return 0;
    N.result_due = 0;
    *result = N.res;
    *p1_down = N.down[0];
    *p2_down = N.down[1];
    return 1;
}

rogue_command rogue_tick(uint64_t now_ms, int in_fight) {
    N.now = now_ms;
    if (N.closing) { N.closing = 0; return RC_NET_CLOSE; }
    if (!N.link) return RC_NONE;
    net_link_service(N.link, now_ms, in_fight);
    int n;
    while (N.link && (n = net_link_msg_recv(N.link, g_net_buf, sizeof g_net_buf)) > 0) {
        if (guest()) guest_got(g_net_buf, n);
        else host_got(g_net_buf, n);
    }
    if (N.link && net_link_peer_left(N.link))
        net_end("THE OTHER PLAYER LEFT. THE RUN IS SAVED ON THE HOST'S PC: HOST AND JOIN AGAIN TO GO ON.");
    if (N.link && net_link_quiet_ms(N.link, now_ms) > (in_fight ? 15000u : 10000u))
        net_end("CONNECTION LOST. THE RUN IS SAVED ON THE HOST'S PC: HOST AND JOIN AGAIN TO GO ON.");
    if (!N.link) { N.closing = 0; return RC_NET_CLOSE; }
    if (!guest()) {
        if (!in_fight && !R.borrow) host_try_fight();
        net_flush();                          /* the run first, then where the host is */
        const int menu = C.screen == CS_MENU && R.screen == RS_COOP;
        if (menu != N.sent_menu) {
            const uint8_t b = (uint8_t)menu;
            net_send('P', &b, 1);
            N.sent_menu = menu;
        }
    }
    if (!in_fight && C.screen == CS_VS && N.fight_at && now_ms >= N.fight_at && R.screen == RS_COOP) {
        N.fight_at = 0;
        return RC_FIGHT;
    }
    return RC_NONE;
}
