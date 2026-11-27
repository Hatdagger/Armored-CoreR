/* Split screen co-op: the step between "yes, sortie" and the fight, when
 * player 1 confirms a mission or an Arena challenge. Player 1 picks
 * SORTIE TOGETHER (WAIT FOR PLAYER 2 when player 2 sat the last one out)
 * or GO SOLO; going together needs player 2's A too (both agree). Player 2
 * can press A at any time on this screen: that quick-joins them, straight
 * into the launch once player 1 has said together. B (player 1) backs out;
 * B (player 2) says "not this one". */
#ifndef PARTNER_GATE_H
#define PARTNER_GATE_H

#include "garage.h"

typedef enum partner_result {
    PG_NONE,
    PG_TOGETHER,   /* both agreed: the fight with player 2 */
    PG_SOLO,       /* player 1 alone (player 2 sits this one out) */
    PG_BACK,       /* player 1 backed out: back to the confirmation */
} partner_result;

typedef struct partner_gate {
    int open;
    int p2_in;       /* player 2 played the last one (else they sat out / left) */
    int pick;        /* player 1's cursor: 0 together / wait, 1 solo */
    int waiting;     /* player 1 picked together: waiting for player 2's A */
    int p2_ok;       /* player 2 pressed A */
    int p2_no;       /* player 2 pressed B */
    char what[48];   /* "MISSION" / "ARENA FIGHT" */
} partner_gate;

void partner_gate_open(partner_gate *g, int p2_in, const char *what);
partner_result partner_gate_input(partner_gate *g, int who, garage_action a);
void partner_gate_draw(const partner_gate *g, float hud_w);

#endif
