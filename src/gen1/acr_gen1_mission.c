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

int acr_gen1_mission_map_ok(int map) {
    return (map >= 0 && map < 20) || (map >= ACR_GEN1_PP_MAP_BASE && map < ACR_GEN1_PP_MAP_BASE + ACR_GEN1_PP_MAPS);
}

int acr_gen1_mission_has_script(int map) { return acr_gen1_mission_map_ok(map); }

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

int acr_gen1_mission_wait_flags(int map, int record, unsigned flags) {
    if (!acr_gen1_mission_has_script(map)) return ACR_GEN1_WAIT_NONE;
    if (map == 6 && record == MAP6_BIG_MT) return ACR_GEN1_WAIT_SHOWN;
    if (flags & ACR_GEN1_RECORD_LATE) return ACR_GEN1_WAIT_HIDDEN;
    return acr_gen1_mission_wait(map, record);
}

int acr_gen1_mission_clock_ticks(int map) { return map == 3 ? 6600 : map == 14 ? 3960 : 0; }

static acr_gen1_unit *by_record(acr_gen1_battle *b, int record) {
    for (int u = 0; u < b->units; ++u)
        if (b->unit[u].record == record) return &b->unit[u];
    return NULL;
}

static int destroyed(const acr_gen1_unit *u) { return u->down || u->ap <= 0; }

void acr_gen1_mission_start(acr_gen1_battle *b, int map) {
    memset(&b->mission, 0, sizeof b->mission);
    b->gim.stage = 0;    /* the platform reads the map's doors again: all shut */
    if (acr_gen1_mission_has_script(map)) b->mission.map = (uint8_t)(map + 1);
    b->mission.clock = (uint16_t)acr_gen1_mission_clock_ticks(map);
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
        m->end_ticks = (uint16_t)(m->flags & ACR_GEN1_MISSION_BONUS ? END_TICKS : MAP6_FADE_TICKS + END_TICKS);
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
    m->end_ticks = (uint16_t)(map == 15 ? END_TICKS : PLAIN_WAIT + END_TICKS);
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

/* ---- The other maps (read 2026-11-27) ----
 * Slot 0 of maps 0, 1, 3-5, 7-11, 14 and 16-19 (docs/research/gen1-stages.md,
 * "Mission scripts, all maps"). Places are the game's (x, y down, z); the
 * battle keeps x the other way round (engine x = -game x). A record named
 * here waits hidden when its flags say it isn't made at the start
 * (ACR_GEN1_RECORD_LATE) and joins when its step comes.
 *
 * Read from the game, with the game's tick counts:
 * - map 1 "Defend Submarine": records 10-12 (two gun turrets and a box
 *   structure) down -> 660 ticks of radio (26, 27) -> once the radio is done
 *   and the player stands on the ship (its areas 54-56, x 3,750-11,250,
 *   z -1,250-1,250, on the deck at -250) -> accomplished. The player below
 *   the sea's surface (y past 256) -> failed (radio 158).
 * - map 3 "Destroy Cannon": a 5:00 clock (6,600 ticks); the reverse-joint
 *   AC (record 5) down -> 220 ticks (a fade, record 7 flies off in
 *   cutscene 2) -> accomplished; the clock out -> 18 ticks of fade -> failed.
 * - map 4: the five hover guns (records 5-9) down -> 20 ticks, radio 94,
 *   once it is done record 4 (the AC, cutscene 1) joins; it down -> radio 9,
 *   20 ticks with the radio -> accomplished.
 * - map 5: records 4-63 still standing (made or not) under 10 -> 20 ticks
 *   -> records 15-22 (the eight tanks) join; under 2 -> 159 ticks ->
 *   cutscene 0 brings the AC (record 5); it down -> 30 ticks -> accomplished.
 * - map 7: the player in area 39 (x 7,675-8,575, y -1,050-2,600, z
 *   5,362-5,937; the object's area number +0x30) -> the helicopter (record
 *   12) joins; the box vehicle (record 5) down -> accomplished.
 * - map 8: the AC (record 4) down -> 20 ticks (radio 44) -> the player in
 *   point 4's box (x -4,768 to -2,976, y -1,780 to 12, z -9,536 to -7,232:
 *   chunk 10's point plus its six signed bytes x 256) -> accomplished.
 * - map 10: the player in point 0's box (x 2,900-5,972, y 13,071-14,351,
 *   z -694 to -182: where the research team is) -> 50 ticks of held
 *   controls, cutscene 2 -> accomplished. The AC (record 5) down is a bonus
 *   (event flag 15).
 * - map 11: both quad ACs (records 4, 5; made at the start) down ->
 *   accomplished. Map 19 (the last one, no briefing): its AC (record 4,
 *   made at the start) down -> accomplished.
 * - map 14: a 3:00 clock (3,960 ticks); the AC (record 4) down ->
 *   accomplished; the clock out -> radio 62 and, once it is done, failed.
 * - map 17: two tall MTs at a time: 4 and 5 down -> 6 and 7 join; down -> 8
 *   and 9; down -> the AC (record 10); its AP at 0 -> accomplished.
 * - map 18: two ACs (records 4, 5) join when stage area 68's gimmick
 *   triggers; both down -> radio 114, the doors (areas 92 / 93) open; area
 *   96's gimmick (the core) -> a 64-tick fade -> accomplished.
 *
 * Stand-ins, where the game's trigger lives in a stage gimmick (an area
 * record's controller, +0x2C, its state +16) or object code not read yet:
 * - map 0: the game ends on area 290's gimmick (a door by the botanical
 *   zone); here the Armored Car (record 4, AP 1) down ends it.
 * - map 1: the submarine's hull (areas 54-56, breakable) sinking fails it in
 *   the game; the engine has no breakable areas, so only the sea fails it.
 * - map 3: record 5 is made by cutscene 1 (with record 4, the jet); what
 *   starts that step (a counter, 0x800AB868, reaching 4) is taken as four of
 *   the eight drones (records 8-15) down, or every unit in the fight down.
 *   Then 360 ticks (radio 30) and both join.
 * - map 9 "Recover the secret material": the tank-legged AC (record 4) down
 *   opens the container (area 169); the ending isn't in slot 0 (no
 *   0x80052490 there). Here: the AC down, then the player within 1,000 of
 *   the container -> 200 ticks -> accomplished.
 * - map 16: records 4-11 (eight MTs) and the AC (record 30) aren't made at
 *   the start; the AC comes on area 24's gimmick (a switch). Here: everything
 *   in the fight down -> 4-11 join; those down -> the AC joins; the AC down
 *   -> 300 ticks (radio 69, 70) -> accomplished, as the game's.
 * - map 18: the areas' gimmicks are taken as the player inside the area's
 *   box, grown by 750 for the small switch (area 68).
 * - "Once the radio is done": the radio's own length isn't read; RADIO_TICKS
 *   (3 s) as for map 12. */
#define RADIO_TICKS MAP12_RADIO_TICKS

typedef struct game_box { int16_t x0, y0, z0, x1, y1, z1; } game_box;
static const game_box k_map1_ship  = { 3750, -2500, -1250, 11250, 625, 1250 };
static const game_box k_map7_area39 = { 7675, -1050, 5362, 8575, 2600, 5937 };
static const game_box k_map8_exit  = { -4768, -1780, -9536, -2976, 12, -7232 };
static const game_box k_map10_team = { 2900, 13071, -694, 5972, 14351, -182 };
static const game_box k_map18_switch = { -5625, -3125, 18285, -5250, -2812, 18465 };   /* area 68 */
static const game_box k_map18_core = { -500, -2688, 21875, 500, 5750, 22875 };          /* area 96 */
static const int16_t k_map9_container[3] = { -5406, 28416, -2293 };                     /* area 169's centre */

/* A player (team 0, standing) inside box g grown by `grow`, as the game's
 * strict test (min < p < max). */
static int player_in(const acr_gen1_battle *b, const game_box *g, int grow) {
    const int n = acr_gen1_battle_fighters(b);
    for (int i = 0; i < n; ++i) {
        const acr_gen1_fighter *f = &b->f[i];
        if (acr_gen1_battle_team(b, i) != 0 || f->ap <= 0) continue;
        const int32_t x = -f->ac.x / 64, y = f->ac.y / 64, z = f->ac.z / 64;
        if (x > g->x0 - grow && x < g->x1 + grow && y > g->y0 - grow && y < g->y1 + grow &&
            z > g->z0 - grow && z < g->z1 + grow) return 1;
    }
    return 0;
}

static int player_near(const acr_gen1_battle *b, const int16_t *p, int32_t r) {
    const int n = acr_gen1_battle_fighters(b);
    for (int i = 0; i < n; ++i) {
        const acr_gen1_fighter *f = &b->f[i];
        if (acr_gen1_battle_team(b, i) != 0 || f->ap <= 0) continue;
        const int64_t dx = -f->ac.x / 64 - p[0], dy = f->ac.y / 64 - p[1], dz = f->ac.z / 64 - p[2];
        if (dx * dx + dy * dy + dz * dz < (int64_t)r * r) return 1;
    }
    return 0;
}

/* Records first..last all destroyed (a record the engine has no unit for
 * counts as destroyed, as one with no handler does in the game). */
static int all_down(acr_gen1_battle *b, int first, int last) {
    for (int r = first; r <= last; ++r) {
        const acr_gen1_unit *u = by_record(b, r);
        if (u && !destroyed(u)) return 0;
    }
    return 1;
}

static int down_count(acr_gen1_battle *b, int first, int last) {
    int n = 0;
    for (int r = first; r <= last; ++r) {
        const acr_gen1_unit *u = by_record(b, r);
        n += u && destroyed(u);
    }
    return n;
}

static void join_records(acr_gen1_battle *b, int first, int last) {
    for (int u = 0; u < b->units; ++u)
        if (b->unit[u].record >= first && b->unit[u].record <= last && b->unit[u].wait) join(&b->unit[u]);
}

/* No enemy unit in the fight still standing. */
static int fight_clear(const acr_gen1_battle *b) {
    for (int u = 0; u < b->units; ++u) {
        const acr_gen1_unit *un = &b->unit[u];
        if (un->active && (un->team & 1) && !destroyed(un)) return 0;
    }
    return 1;
}

static int rec_down(acr_gen1_battle *b, int r) { return all_down(b, r, r); }

static void finish(acr_gen1_mission *m, int wait, int failed) {
    if (failed) m->flags |= ACR_GEN1_MISSION_FAIL;
    m->end_ticks = (uint16_t)(wait + END_TICKS);
}

/* Counts a timed step down; 1 the tick it runs out. */
static int count_down(uint16_t *t) { return *t && !--*t; }

static void moa_tick(acr_gen1_battle *b, int map) {
    acr_gen1_mission *m = &b->mission;
    /* The mission clock (0x80090FD8; maps 3 and 14). */
    if (m->clock && !--m->clock && m->phase >= 0) {
        if (map == 3) { finish(m, 18, 1); return; }
        if (map == 14) { finish(m, RADIO_TICKS, 1); return; }
    }
    switch (map) {
    case 0: {
        /* The game: area 290's door (by the botanical zone) starting to
         * open ends it. Without the doors (an import from before them):
         * the Armored Car (record 4) down. */
        const int d = acr_gen1_door_find(&b->gim, 290);
        if (d >= 0 ? b->gim.door[d].state == 1 || b->gim.door[d].state == 3 : rec_down(b, 4))
            finish(m, 0, 0);
        break;
    }
    case 1: {
        const int n = acr_gen1_battle_fighters(b);
        for (int i = 0; i < n; ++i)
            if (acr_gen1_battle_team(b, i) == 0 && b->f[i].ap > 0 && b->f[i].ac.y / 64 > 256) { finish(m, 0, 1); return; }
        if (m->phase == 0 && all_down(b, 10, 12)) { m->phase = 1; m->timer = 660 + RADIO_TICKS; }
        else if (m->phase == 1 && count_down(&m->timer)) m->phase = 2;
        else if (m->phase == 2 && player_in(b, &k_map1_ship, 0)) {
            const int32_t deck = -250;
            for (int i = 0; i < n; ++i)
                if (acr_gen1_battle_team(b, i) == 0 && b->f[i].ap > 0) {
                    const int32_t y = b->f[i].ac.y / 64;
                    if (y - deck < 10 && deck - y < 10) { m->phase = 3; finish(m, 0, 0); return; }
                }
        }
        break;
    }
    case 3:
        if (m->phase == 0 && (down_count(b, 8, 15) >= 4 || fight_clear(b))) { m->phase = 1; m->timer = 360; }
        else if (m->phase == 1 && count_down(&m->timer)) { join_records(b, 4, 5); m->phase = 2; }
        else if (m->phase == 2 && rec_down(b, 5)) { m->phase = -1; finish(m, 220, 0); }
        break;
    case 4:
        if (m->phase == 0 && all_down(b, 5, 9)) { m->phase = 1; m->timer = 20 + RADIO_TICKS; }
        else if (m->phase == 1 && count_down(&m->timer)) { join_records(b, 4, 4); m->phase = 2; }
        else if (m->phase == 2 && rec_down(b, 4)) finish(m, 20 + RADIO_TICKS, 0);
        break;
    case 5: {
        int left = 0;
        for (int u = 0; u < b->units; ++u) left += b->unit[u].record >= 4 && !destroyed(&b->unit[u]);
        if (!(m->step & 1) && left < 10) { m->step |= 1; m->timer2 = 20; }
        if (count_down(&m->timer2)) join_records(b, 15, 22);
        if (!(m->step & 2) && left < 2) { m->step |= 2; m->timer = 159; }
        if (count_down(&m->timer)) join_records(b, 5, 5);
        if ((m->step & 2) && !m->timer && rec_down(b, 5)) finish(m, 30, 0);
        break;
    }
    case 7:
        if (!m->step && player_in(b, &k_map7_area39, 0)) { m->step = 1; join_records(b, 12, 12); }
        if (rec_down(b, 5)) finish(m, 0, 0);
        break;
    case 8:
        if (m->phase == 0 && rec_down(b, 4)) { m->phase = 1; m->timer = 20; }
        else if (m->phase == 1 && count_down(&m->timer)) m->phase = 2;
        else if (m->phase == 2 && player_in(b, &k_map8_exit, 0)) finish(m, 0, 0);
        break;
    case 9:
        if (m->phase == 0 && rec_down(b, 4)) m->phase = 1;
        else if (m->phase == 1 && player_near(b, k_map9_container, 1000)) { m->phase = 2; finish(m, 200, 0); }
        break;
    case 10:
        if (rec_down(b, 5)) m->flags |= ACR_GEN1_MISSION_BONUS;
        if (player_in(b, &k_map10_team, 0)) finish(m, 50, 0);
        break;
    case 11:
    case 19:
        if (m->phase == 0) { join_records(b, 4, 5); m->phase = 1; }
        else if (remaining(b) == 0) finish(m, 0, 0);
        break;
    case 14:
        if (rec_down(b, 4)) { m->phase = -1; finish(m, 0, 0); }
        break;
    case 16:
        if (m->phase == 0 && fight_clear(b)) { join_records(b, 4, 11); m->phase = 1; }
        else if (m->phase == 1 && fight_clear(b)) { join_records(b, 30, 30); m->phase = 2; }
        else if (m->phase == 2 && rec_down(b, 30)) finish(m, 300, 0);
        break;
    case 17: {
        static const uint8_t pair[3][2] = { { 4, 5 }, { 6, 7 }, { 8, 9 } };
        if (m->step < 3) {
            if (rec_down(b, pair[m->step][0]) && rec_down(b, pair[m->step][1])) {
                if (++m->step < 3) join_records(b, pair[m->step][0], pair[m->step][1]);
                else join_records(b, 10, 10);
            }
        } else if (rec_down(b, 10)) finish(m, 0, 0);
        break;
    }
    case 18:
        if (m->phase == 0 && player_in(b, &k_map18_switch, 750)) { join_records(b, 4, 5); m->phase = 1; }
        else if (m->phase == 1 && all_down(b, 4, 5)) { m->phase = 2; m->timer2 = RADIO_TICKS; }
        else if (m->phase == 2 && player_in(b, &k_map18_core, 0)) finish(m, 64, 0);
        /* The game (0x800A28B8): both ACs down -> radio 114; once it is
         * done, message 0x8201 to areas 92 and 93 (unlock, open for good). */
        if (m->phase == 2 && count_down(&m->timer2)) {
            acr_gen1_door_unlock(&b->gim, acr_gen1_door_find(&b->gim, 92));
            acr_gen1_door_unlock(&b->gim, acr_gen1_door_find(&b->gim, 93));
        }
        break;
    default:
        /* Project Phantasma's maps (50-66): their scripts aren't read yet.
         * Stand-in: everything in the fight down brings in what waits (the
         * records the map makes later, flag 0x40), all at once; once that is
         * down too, accomplished. */
        if (m->phase == 0 && fight_clear(b)) {
            int waiting = 0;
            for (int u = 0; u < b->units; ++u)
                if (b->unit[u].wait == ACR_GEN1_WAIT_HIDDEN && !destroyed(&b->unit[u])) { join(&b->unit[u]); ++waiting; }
            m->phase = 1;
            if (!waiting) finish(m, 0, 0);
        } else if (m->phase == 1 && fight_clear(b)) finish(m, 0, 0);
        break;
    }
}

int acr_gen1_mission_players_down(const acr_gen1_battle *b) {
    const int n = acr_gen1_battle_fighters(b);
    for (int i = 0; i < n; ++i)
        if (acr_gen1_battle_team(b, i) == 0 && b->f[i].ap > 0) return 0;
    return 1;
}

int acr_gen1_mission_foe(const acr_gen1_battle *b) { return b->mission.foe ? b->mission.foe : 1; }

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
    const acr_gen1_fighter *foe = &b->f[acr_gen1_mission_foe(b)];
    if (m->own != ACR_GEN1_OWN_P4_ALTERED) return;
    if (m->phase == 0 && (int64_t)foe->ap * 2 < foe->ap_max) {
        m->phase = 1;
        m->flags |= ACR_GEN1_MISSION_SWAP;
    } else if (m->phase == 2 && foe->ap <= 0) m->flags |= ACR_GEN1_MISSION_BONUS;
}

void acr_gen1_mission_tick(acr_gen1_battle *b) {
    acr_gen1_mission *m = &b->mission;
    if (m->own) { if (!acr_gen1_mission_players_down(b)) own_tick(b); return; }
    if (!m->map || m->result) return;
    if (acr_gen1_mission_players_down(b)) return;   /* the player (co-op: both) is down: the script stops */
    if (m->end_ticks) {
        if (!--m->end_ticks)
            m->result = m->flags & ACR_GEN1_MISSION_FAIL ? ACR_GEN1_MISSION_FAILED : ACR_GEN1_MISSION_DONE;
        return;
    }
    const int map = m->map - 1;
    if (map == 6) map6_tick(b);
    else if (map == 12) map12_tick(b);
    else if (map == 2 || map == 13 || map == 15) plain_tick(b, map);
    else moa_tick(b, map);
}
