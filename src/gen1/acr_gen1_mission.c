/* Mission scripts (acr_gen1_mission.h). Map 6, "PROGTECH Marine
 * Laboratory", from its slot 0 (0x800A641C) and the cutscenes it starts:
 *
 * - Records: 4 the tank-legged AC, 5 a model-less object beside it, 6 the
 *   big MT (U19) 1,000 under the deck, 7-11 the five MTs.
 * - Step 0: when every object but 4, 5 and 6 is destroyed (entry state 3,
 *   or no handler), two stage nodes (132 / 133) start their animation 4:
 *   the deck opens. Step 1.
 * - Step 1: when node 132's animation is done (its controller's +16), the
 *   tank AC is made (0x8007ECEC) and object 5 removed, cutscenes 1 and 2
 *   run, the big MT's per-tick hook becomes the splash and shake of its
 *   climb (0x800A5F5C) and cutscene 3 brings it up: in that cutscene it
 *   plays animations 26, 29 and 27 (no root motion in any of them) while
 *   the cutscene moves it between points 10-13. Radio line 37. Step -1.
 * - Step -1: the tank AC destroyed (or fallen past y 30,000) sets event
 *   flag 18 (0x800907D0; a bonus, it isn't needed) and radio line 123.
 * - The big MT destroyed ends the mission: with the tank AC already down,
 *   radio 153 and the accomplished request at once (0x80052490(128): the
 *   ending starts 100 ticks later); with it still up, a 60-tick fade to
 *   white, cutscene 4 (the tank AC leaves) and the same request.
 * - The player's AP at 0 stops the script (the battle loses the mission).
 *
 * - The climb (cutscene 3's object track, 270 ticks): after 50 ticks the
 *   big MT is put at point 10 (animation 26), keyed to point 11 over 60
 *   ticks, then (animation 29) to point 12 over 34, then put at point 13
 *   facing 2048 (animation 27) for the last 126. The points are chunk 10's
 *   (copied to 0x801E49D8 by 0x80078DE8; game x, y down, z):
 *   10 (300, 500, 11000), 11 (300, 500, 8000), 12 (300, -1200, 6500),
 *   13 (300, -500, 3500), the deck. It joins the fight when it ends.
 *
 * Stand-ins (not read yet): the deck's opening time (node 132's animation 4;
 * cutscene 2 sets nodes 132 / 133 to frame 161, perhaps its end) is
 * MAP6_OPEN_TICKS; a position key is taken as a straight line over the
 * wait that follows it (the key interpolation is not read). The fight
 * doesn't pause for the cutscenes as the game's does. */
#include "acr_gen1_mission.h"

#include <string.h>

#include "acr_gen1_battle.h"

#define MAP6_TANK        4
#define MAP6_SPARE       5
#define MAP6_BIG_MT      6
#define MAP6_OPEN_TICKS  44     /* stand-in: about 2 seconds */
/* Cutscene 3's object track for the big MT: from tick `at`, from point
 * `from` (or where it is, -1) to point `to` over `ticks`, playing `anim`. */
typedef struct map6_key { uint16_t at, ticks; int8_t from, to; uint8_t anim; } map6_key;
static const int16_t k_map6_point[4][3] = {      /* points 10-13, engine x (= -game x), y down, z */
    { -300, 500, 11000 }, { -300, 500, 8000 }, { -300, -1200, 6500 }, { -300, -500, 3500 } };
static const map6_key k_map6_climb[] = {
    { 50, 60, 10, 11, 26 }, { 110, 34, -1, 12, 29 }, { 144, 126, 13, 13, 27 } };
#define MAP6_CLIMB_TICKS 270    /* cutscene 3's length */
#define MAP6_CLIMB_YAW   2048   /* its 1002 rotation at point 13 */
#define MAP6_FADE_TICKS  60     /* the fade before cutscene 4 (slot 0's -10172) */
#define END_TICKS        100    /* 0x80052490: the ending starts this many ticks after the request */

/* Maps 2, 13 and 15 (read 2026-11-24, slot 0 at 0x800A39B0 / 0x800A4FA0 /
 * 0x800A29E0): the plain objective, with the game's own timing. Each
 * counts records 4-63 whose entry has a handler and isn't destroyed
 * (state 3), made or not:
 * - map 2 ("Destroy all combat MTs"): none left -> 29 ticks -> radio 153
 *   and the accomplished request (the ending 100 ticks later);
 * - map 15 ("Tank x 1"): none left -> the request at once;
 * - map 13 ("Trucks x 3, MTs x 4"): fewer than 8 left -> radio 93 and
 *   records 11-14 (four drones, U10) are made, once; none left -> as map 2.
 * Radio lines 28, 120, 121 (map 2) and the intro cutscenes are left out. */
#define MAP13_REINFORCE_BELOW 8
#define PLAIN_WAIT 29           /* maps 2 and 13: the counter starts at 1 that tick and requests at 30 */

/* Map 12, "Save friendly units" (read 2026-11-25, slot 0 at 0x800A4640).
 * Records 4-9 (list 0x800AA0D8) are the targets (handler 1, game team 2),
 * 10-12 (list 0x800AA0F0) the friendly MTs (handler 2, game team 4: the
 * player's side here). Its state 0x800AA100, each tick:
 * - 0: every target destroyed -> 1. Otherwise the friendlies: each one
 *   destroyed plays radio 102-104 once (flags 0x800AA110); none left -> 2.
 * - 1: radio 153 and the accomplished request (0x80052490(128)). -> -1.
 * - 2: a counter (0x800AA10C) = 20. -> -1. The counter counts down that
 *   tick too; at 0, radio 59 (counter -2); once the radio is done
 *   (0x80058480), radio 158 and the failed request (0x80052490(256)).
 * Radio 101 at tick 20 and the friendlies' radio are left out. Stand-in:
 * radio 59's length (MAP12_RADIO_TICKS; the radio's own timing is not
 * read). A record the engine left out (not on a floor) counts as down for
 * the targets and as standing for the friendlies, so it decides nothing. */
#define MAP12_FIRST_TARGET 4
#define MAP12_LAST_TARGET  9
#define MAP12_FIRST_FRIEND 10
#define MAP12_LAST_FRIEND  12
#define MAP12_FAIL_WAIT    19   /* 20, counted down the tick it is set */
#define MAP12_RADIO_TICKS  66   /* stand-in: radio 59, about 3 seconds */

int acr_gen1_mission_has_script(int map) { return map == 2 || map == 6 || map == 12 || map == 13 || map == 15; }

int acr_gen1_mission_team(int map, int record) {
    return map == 12 && record >= MAP12_FIRST_FRIEND && record <= MAP12_LAST_FRIEND ? 0 : 1;
}

int acr_gen1_mission_wait(int map, int record) {
    if (map == 13 && record >= 11 && record <= 14) return ACR_GEN1_WAIT_HIDDEN;
    if (map == 6) {
        if (record == MAP6_TANK) return ACR_GEN1_WAIT_HIDDEN;
        if (record == MAP6_BIG_MT) return ACR_GEN1_WAIT_SHOWN;
    }
    return ACR_GEN1_WAIT_NONE;
}

static acr_gen1_unit *by_record(acr_gen1_battle *b, int record) {
    for (int u = 0; u < b->units; ++u)
        if (b->unit[u].record == record) return &b->unit[u];
    return NULL;
}

static int destroyed(const acr_gen1_unit *u) { return u->down || u->ap <= 0; }

void acr_gen1_mission_start(acr_gen1_battle *b, int map) {
    memset(&b->mission, 0, sizeof b->mission);
    if (acr_gen1_mission_has_script(map)) b->mission.map = (uint8_t)(map + 1);
}

static void join(acr_gen1_unit *u) {
    u->active = 1;
    u->wait = ACR_GEN1_WAIT_NONE;
}

static void map6_put(acr_gen1_unit *u, const int16_t *p) {
    u->x = p[0] * 64; u->y = p[1] * 64; u->z = p[2] * 64;
}

/* Tick t of cutscene 3 for the big MT (see the top of the file). */
static void map6_climb(acr_gen1_mission *m, acr_gen1_unit *u, unsigned t) {
    u->px = u->x; u->py = u->y; u->pz = u->z;
    if (t >= MAP6_CLIMB_TICKS) {
        map6_put(u, k_map6_point[3]);
        u->px = u->x; u->py = u->y; u->pz = u->z;
        u->yaw = MAP6_CLIMB_YAW;
        u->anim = 0;
        join(u);
        return;
    }
    for (int k = (int)(sizeof k_map6_climb / sizeof k_map6_climb[0]) - 1; k >= 0; --k) {
        const map6_key *key = &k_map6_climb[k];
        if (t < key->at) continue;
        const unsigned n = t - key->at;
        if (n == 0) {                                   /* the key starts */
            if (key->from >= 0) {
                map6_put(u, k_map6_point[key->from - 10]);
                u->px = u->x; u->py = u->y; u->pz = u->z;
            }
            if (key->to == 13) u->yaw = MAP6_CLIMB_YAW;
            u->anim = key->anim;
            u->anim_frame = 0;
            m->from[0] = u->x; m->from[1] = u->y; m->from[2] = u->z;
        }
        const int16_t *to = k_map6_point[key->to - 10];
        const int32_t left = (int32_t)key->ticks - (int32_t)n - 1;   /* reached on the key's last tick */
        u->x = to[0] * 64 + (int32_t)((int64_t)(m->from[0] - to[0] * 64) * left / key->ticks);
        u->y = to[1] * 64 + (int32_t)((int64_t)(m->from[1] - to[1] * 64) * left / key->ticks);
        u->z = to[2] * 64 + (int32_t)((int64_t)(m->from[2] - to[2] * 64) * left / key->ticks);
        ++u->anim_frame;
        return;
    }
}

static void map6_tick(acr_gen1_battle *b) {
    acr_gen1_mission *m = &b->mission;
    acr_gen1_unit *tank = by_record(b, MAP6_TANK), *big = by_record(b, MAP6_BIG_MT);
    if (m->phase == 0) {
        for (int u = 0; u < b->units; ++u) {
            const acr_gen1_unit *un = &b->unit[u];
            if (un->record == MAP6_TANK || un->record == MAP6_SPARE || un->record == MAP6_BIG_MT) continue;
            if (!destroyed(un)) return;
        }
        m->phase = 1;
        m->timer = MAP6_OPEN_TICKS;
        return;
    }
    if (m->phase == 1) {
        if (m->timer && --m->timer) return;
        if (tank && tank->wait) join(tank);
        m->phase = -1;
        m->timer = 0;                           /* counts cutscene 3's ticks */
        return;
    }
    /* Step -1: the big MT climbs (cutscene 3), then the fight runs to its end. */
    if (big && big->wait == ACR_GEN1_WAIT_SHOWN) map6_climb(m, big, m->timer++);
    if (tank && !tank->wait && destroyed(tank)) m->flags |= ACR_GEN1_MISSION_BONUS;
    if (!m->end_ticks && big && !big->wait && destroyed(big))
        m->end_ticks = (uint8_t)(m->flags & ACR_GEN1_MISSION_BONUS ? END_TICKS : MAP6_FADE_TICKS + END_TICKS);
}

/* Records 4 on still standing, made or waiting (slot 0's count). */
static int remaining(const acr_gen1_battle *b) {
    int n = 0;
    for (int u = 0; u < b->units; ++u) {
        const acr_gen1_unit *un = &b->unit[u];
        if (un->record >= 4 && !destroyed(un)) ++n;
    }
    return n;
}

static void plain_tick(acr_gen1_battle *b, int map) {
    acr_gen1_mission *m = &b->mission;
    const int left = remaining(b);
    if (map == 13 && m->phase == 0 && left < MAP13_REINFORCE_BELOW) {
        m->phase = 1;                           /* radio 93; records 11-14 made (0x8007ECEC) */
        for (int u = 0; u < b->units; ++u)
            if (b->unit[u].record >= 11 && b->unit[u].record <= 14 && b->unit[u].wait) join(&b->unit[u]);
        return;
    }
    if (left) return;
    m->end_ticks = (uint8_t)(map == 15 ? END_TICKS : PLAIN_WAIT + END_TICKS);
}

static void map12_tick(acr_gen1_battle *b) {
    acr_gen1_mission *m = &b->mission;
    if (m->phase == -2) {                       /* the failed request after radio 59 */
        if (m->timer && --m->timer) return;
        m->flags |= ACR_GEN1_MISSION_FAIL;
        m->end_ticks = END_TICKS;
        m->phase = -3;
        return;
    }
    if (m->phase) return;
    int targets = 0, friends = 0;
    for (int r = MAP12_FIRST_TARGET; r <= MAP12_LAST_TARGET; ++r) {
        const acr_gen1_unit *u = by_record(b, r);
        targets += u && !destroyed(u);
    }
    if (!targets) {                             /* radio 153, accomplished */
        m->end_ticks = END_TICKS;
        m->phase = -1;
        return;
    }
    for (int r = MAP12_FIRST_FRIEND; r <= MAP12_LAST_FRIEND; ++r) {
        const acr_gen1_unit *u = by_record(b, r);
        friends += !u || !destroyed(u);
    }
    if (friends) return;
    m->phase = -2;                              /* 20 ticks, radio 59, then failed */
    m->timer = MAP12_FAIL_WAIT + MAP12_RADIO_TICKS;
}

void acr_gen1_mission_start_own(acr_gen1_battle *b, int own) {
    memset(&b->mission, 0, sizeof b->mission);
    b->mission.own = (uint8_t)own;
}

void acr_gen1_mission_challenger_in(acr_gen1_battle *b) {
    acr_gen1_mission *m = &b->mission;
    if (m->own != ACR_GEN1_OWN_P4_ALTERED || m->phase != 1) return;
    m->flags &= (uint8_t)~ACR_GEN1_MISSION_SWAP;
    m->phase = 2;
}

/* P4 Altered: the examiner below half AP brings the challenger (see the .h). */
static void own_tick(acr_gen1_battle *b) {
    acr_gen1_mission *m = &b->mission;
    const acr_gen1_fighter *foe = &b->f[1];
    if (m->own != ACR_GEN1_OWN_P4_ALTERED) return;
    if (m->phase == 0 && (int64_t)foe->ap * 2 < foe->ap_max) {
        m->phase = 1;
        m->flags |= ACR_GEN1_MISSION_SWAP;
    } else if (m->phase == 2 && foe->ap <= 0) m->flags |= ACR_GEN1_MISSION_BONUS;
}

void acr_gen1_mission_tick(acr_gen1_battle *b) {
    acr_gen1_mission *m = &b->mission;
    if (m->own) { if (b->f[0].ap > 0) own_tick(b); return; }
    if (!m->map || m->result) return;
    if (b->f[0].ap <= 0) return;                /* the player is down: the script stops */
    if (m->end_ticks) {
        if (!--m->end_ticks)
            m->result = m->flags & ACR_GEN1_MISSION_FAIL ? ACR_GEN1_MISSION_FAILED : ACR_GEN1_MISSION_DONE;
        return;
    }
    if (m->map == 7) map6_tick(b);
    else if (m->map == 13) map12_tick(b);
    else plain_tick(b, m->map - 1);
}
