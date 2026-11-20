/* The internet connection screens, with briefing codes (acr_brief.h):
 *
 *  - the editor: entering a code field by field, each field a drop-down list
 *    (controller first; the keyboard types to jump in a list, and a code can
 *    be pasted whole);
 *  - player 1's screen: the MISSION BRIEFING to send, and the way in to the
 *    read-back if player 2 needs one;
 *  - player 2's screen: connecting, and if that fails an URGENT MISSION
 *    REQUEST, then the READ-BACK to send to player 1.
 * Both codes can always be copied (the caller puts the text on the
 * clipboard). Draws on the HUD grid like the other network screens. */
#ifndef NET_BRIEF_UI_H
#define NET_BRIEF_UI_H

#include <stdint.h>

#include "../../net/acr_brief.h"
#include "garage.h"
#include "net_link.h"

typedef enum brief_result { BE_NONE, BE_DONE, BE_CANCEL, BE_PASTE } brief_result;

typedef struct brief_edit {
    acr_brief b;
    int kind;                    /* what is being entered: ACR_BRIEF_OFFER or _REPLY */
    int row;                     /* 0..ACR_BRIEF_FIELDS-1 fields, then CONFIRM, PASTE, CANCEL */
    int open;                    /* the row's drop-down is open */
    int pick;                    /* the drop-down's cursor */
    char typed[16];              /* keyboard: letters typed to jump in the list */
    uint64_t typed_ms;
    char message[200];
    int message_ms;
} brief_edit;

void brief_edit_start(brief_edit *e, int kind);
/* A menu action. BE_DONE: the code checks out (address in `to`). BE_PASTE:
 * the caller reads the clipboard and calls brief_edit_paste. */
brief_result brief_edit_input(brief_edit *e, garage_action a, net_addr *to);
/* Typed text: jumps in the open list (or opens the row's list). */
void brief_edit_text(brief_edit *e, const char *text, uint64_t now_ms);
/* A whole code pasted. 1 if it was read (the fields are filled in). */
int brief_edit_paste(brief_edit *e, const char *text);
void brief_edit_say(brief_edit *e, const char *text);
void brief_edit_draw(brief_edit *e, float hud_w, int elapsed_ms);

/* Player 1, waiting. `stun`: net_link_public's result; `me` our internet
 * address when stun == 1; `readback`: a read-back has been entered. */
void brief_draw_host(float hud_w, int stun, const net_addr *me, int readback, int waited_s,
                     const char *message, int elapsed_ms);
/* Player 2, connecting. `since_ms`: time since the briefing was entered. */
void brief_draw_guest(float hud_w, int stun, const net_addr *me, uint64_t since_ms, const char *message);
/* Player 2 escalates to a read-back after this long without an answer. */
#define BRIEF_TRY_MS 5000
#define BRIEF_URGENT_MS 2400

/* The text to copy for a code (one line). */
void brief_copy_text(const net_addr *a, int kind, char *out, int size);

#endif
