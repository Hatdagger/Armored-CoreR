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
 * Stand-ins (not read yet): the deck's opening time (node 132's animation)
 * is MAP6_OPEN_TICKS; the cutscene points are run-time stage data, so the
 * big MT climbs straight up at MAP6_RISE a tick to the MTs' deck height
 * (y -500) and joins the fight there. The cutscenes themselves are
 * skipped (the game pauses the fight for them). */
#include "acr_gen1_mission.h"

#include <string.h>

#include "acr_gen1_battle.h"

#define MAP6_TANK        4
#define MAP6_SPARE       5
#define MAP6_BIG_MT      6
#define MAP6_OPEN_TICKS  44     /* stand-in: about 2 seconds */
#define MAP6_RISE        17     /* stand-in: 1,000 in about 60 ticks (cutscene 3's first move) */
#define MAP6_DECK_Y      (-500) /* the five MTs' records stand here */
#define MAP6_FADE_TICKS  60     /* the fade before cutscene 4 (slot 0's -10172) */
#define END_TICKS        100    /* 0x80052490: the ending starts this many ticks after the request */

int acr_gen1_mission_has_script(int map) { return map == 6; }

int acr_gen1_mission_wait(int map, int record) {
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
        return;
    }
    /* Step -1: the big MT climbs, then the fight runs to its end. */
    if (big && big->wait == ACR_GEN1_WAIT_SHOWN) {
        big->py = big->y;
        big->y -= MAP6_RISE * 64;
        if (big->y <= MAP6_DECK_Y * 64) {
            big->y = MAP6_DECK_Y * 64;
            big->py = big->y;
            join(big);
        }
    }
    if (tank && !tank->wait && destroyed(tank)) m->flags |= ACR_GEN1_MISSION_BONUS;
    if (!m->end_ticks && big && !big->wait && destroyed(big))
        m->end_ticks = (uint8_t)(m->flags & ACR_GEN1_MISSION_BONUS ? END_TICKS : MAP6_FADE_TICKS + END_TICKS);
}

void acr_gen1_mission_tick(acr_gen1_battle *b) {
    acr_gen1_mission *m = &b->mission;
    if (!m->map || m->result) return;
    if (b->f[0].ap <= 0) return;                /* the player is down: the script stops */
    if (m->end_ticks) {
        if (!--m->end_ticks) m->result = ACR_GEN1_MISSION_DONE;
        return;
    }
    if (m->map == 7) map6_tick(b);
}
