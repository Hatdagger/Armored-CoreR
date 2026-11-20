/* Briefing codes: an internet connection code shown as a mission briefing.
 *
 *   MISSION BRIEFING
 *   Destroy Cobalt Reach underground fortress squatters.
 *   Payout: 48,000 C   Time: 03:40   Wingman: Vesper
 *
 * The code holds 64 bits: an IPv4 address (32), a UDP port (16) and a check
 * (16) that catches typos and tells a briefing from a reply. The fields are
 * a mixed-radix number: 6 word lists (64, 64, 128, 128, 128, 128 entries,
 * 40 bits), payout 1,000-99,000 C (99), time 00:00-23:59 (1440) and the
 * wingman (256), about 65 bits in all. The bits are scrambled first so
 * neighbouring addresses give unrelated briefings.
 *
 * The host shows a BRIEFING (its internet address). The joiner enters it and
 * connects; if the host's router blocks that, the joiner's screen shows a
 * READ-BACK (the joiner's address) for the host to enter,
 * and both sides then send to each other so their routers let the match in.
 *
 * No operating-system calls (plain C11), so it ports to the Xbox unchanged. */
#ifndef ACR_BRIEF_H
#define ACR_BRIEF_H

#include <stdint.h>

enum { ACR_BRIEF_OFFER = 0, ACR_BRIEF_REPLY = 1 };

/* The fields, in the order they read. */
enum {
    ACR_BRIEF_ACTION, ACR_BRIEF_PLACE_A, ACR_BRIEF_PLACE_B, ACR_BRIEF_POSITION,
    ACR_BRIEF_STRUCTURE, ACR_BRIEF_TARGET, ACR_BRIEF_PAYOUT, ACR_BRIEF_TIME,
    ACR_BRIEF_WINGMAN, ACR_BRIEF_FIELDS
};

typedef struct acr_brief {
    uint16_t v[ACR_BRIEF_FIELDS];   /* each field's choice, 0 .. acr_brief_choices(field) - 1 */
} acr_brief;

/* The word lists (acr_brief_words.c, alphabetical). */
extern const char *const acr_brief_action[64];
extern const char *const acr_brief_place_a[64];
extern const char *const acr_brief_place_b[128];
extern const char *const acr_brief_position[128];
extern const char *const acr_brief_structure[128];
extern const char *const acr_brief_target[128];
extern const char *const acr_brief_wingman[256];

/* How many choices a field has, its label ("PAYOUT") and a choice as text
 * ("Destroy", "48,000 C", "03:40"; `out` is used for numbers, so keep it
 * until you are done with the result). */
int acr_brief_choices(int field);
const char *acr_brief_label(int field);
const char *acr_brief_choice(int field, int choice, char *out, int size);

/* Address -> briefing (kind: offer or reply). */
void acr_brief_encode(const uint8_t ip[4], uint16_t port, int kind, acr_brief *out);
/* Briefing -> address. 1 when the check passes for that kind, else 0. */
int acr_brief_decode(const acr_brief *b, int kind, uint8_t ip[4], uint16_t *port);

/* One line to copy and paste:
 * "MISSION BRIEFING: Destroy Cobalt Reach underground fortress squatters.
 *  Payout 48,000 C. Time 03:40. Wingman Vesper." (a reply starts
 * "READ-BACK:"). Returns the length. */
int acr_brief_format(const acr_brief *b, int kind, char *out, int size);
/* The sentence alone: "Destroy Cobalt Reach underground fortress squatters." */
int acr_brief_sentence(const acr_brief *b, char *out, int size);
/* Reads pasted or typed text back (any case, extra words and punctuation are
 * fine; payout as 48 or 48,000). 1 if every field was found. */
int acr_brief_parse(const char *text, acr_brief *out);

#endif
