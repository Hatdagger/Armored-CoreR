/* Rollback netplay for a two-player match (1v1 now; co-op next).
 *
 * Each machine runs the match at once with its own player's controls and a
 * guess for the other player's (the last controls it heard, quick presses
 * left out). When the real controls arrive and differ from the guess, it
 * goes back to the snapshot before that step (acr_gen1_match is copied
 * whole before every step) and runs the steps again with the real
 * controls. So your own AC answers at once, and the other one is corrected
 * a few frames later if the guess was wrong.
 *
 * - Controls go out as a 3-byte pad per step, each packet repeating every
 *   step the other side hasn't confirmed yet, so a lost packet costs
 *   nothing but a little time.
 * - Optional input delay (both sides must use the same): fewer corrections,
 *   at the cost of that many steps' lag on your own controls.
 * - A machine never runs more than ACR_RB_WINDOW steps past the other
 *   player's last confirmed controls; past that it waits.
 * - Frame advantage: each side tells the other how far ahead it runs; the
 *   one ahead skips a frame now and then so neither keeps correcting.
 * - Desync check: every ACR_RB_HASH_EVERY steps, once both players'
 *   controls up to there are final, each side sends a hash of the match;
 *   a mismatch is reported (acr_rb_desync).
 *
 * No sockets or clocks in here: the caller moves the packets (UDP on PC,
 * System Link on the Xbox later) and calls acr_rb_tick once a step. */
#ifndef ACR_ROLLBACK_H
#define ACR_ROLLBACK_H

#include <stdint.h>

#include "acr_gen1_match.h"

#define ACR_RB_RING        32    /* snapshots kept (more than the window) */
#define ACR_RB_WINDOW      12    /* steps we may run past the last confirmed remote controls */
#define ACR_RB_MAX_DELAY   8
#define ACR_RB_HASH_EVERY  60    /* steps between desync checks (1 s at 60) */
#define ACR_RB_HASHES      16
#define ACR_RB_PACKET_MAX  176
#define ACR_RB_PADS_MAX    48    /* pads in one packet */

typedef uint32_t acr_rb_pad;   /* 17 bits used, sent as 3 bytes */

/* A pad: everything a player's controller does in one step. */
acr_rb_pad acr_rb_pack(const acr_gen1_input *in, const acr_gen1_actions *act);
void       acr_rb_unpack(acr_rb_pad p, acr_gen1_input *in, acr_gen1_actions *act);
/* The guess for later steps: held controls kept, one-step presses dropped. */
acr_rb_pad acr_rb_held(acr_rb_pad p);

typedef struct acr_rb {
    int      local;                      /* our player: 0 (host) or 1 */
    int      delay;                      /* input delay in steps */
    const acr_gen1_battle *start;
    acr_gen1_match_rules rules;
    acr_gen1_match cur;                  /* the match as shown now */
    uint32_t frame;                      /* the next step to run (= cur.frame) */

    acr_gen1_match saved[ACR_RB_RING];   /* saved[f % RING]: the match before step f */
    acr_rb_pad pad[2][ACR_RB_RING];      /* each player's pad by step */
    uint32_t   pad_for[2][ACR_RB_RING];  /* the step + 1 each slot holds (0 = empty) */
    acr_rb_pad used[ACR_RB_RING];        /* the remote pad the match used for that step */

    uint32_t local_next;                 /* our next step without a pad */
    uint32_t remote_next;                /* their first step we haven't got */
    uint32_t remote_frame;               /* their latest step, as they told us */
    uint32_t remote_ack;                 /* our first step they haven't got */
    int32_t  local_adv256, remote_adv256;/* frame advantage, averaged, 1/256 steps */
    uint32_t rollback_from;              /* step to go back to (UINT32_MAX: none) */

    /* Desync checks */
    uint32_t next_hash;
    uint32_t hash_at[ACR_RB_HASHES], hash_mine[ACR_RB_HASHES], hash_theirs[ACR_RB_HASHES];
    uint8_t  have_mine[ACR_RB_HASHES], have_theirs[ACR_RB_HASHES], checked[ACR_RB_HASHES];
    uint32_t out_hash_frame, out_hash;   /* the latest hash we send */
    uint32_t desync_frame;               /* first step found out of step + 1 (0 = fine) */
    uint32_t checks_ok;                  /* hashes that matched */

    /* Stats for the screen and the tests */
    uint32_t rollbacks, resimulated, deepest, waits;
    uint32_t packets_in, packets_bad;
} acr_rb;

/* A new session. Both machines must pass the same start, rules and delay;
 * `local` is this machine's player. */
void acr_rb_init(acr_rb *rb, int local, int delay, const acr_gen1_battle *start, const acr_gen1_match_rules *rules);

/* One step with our controls: catches up on corrections first, then runs
 * the next step. Returns 1 if it ran, 0 if it must wait for the other
 * player (too far ahead); call again next frame. */
int acr_rb_tick(acr_rb *rb, acr_rb_pad local_pad);

/* Catches up on corrections and checks without running a step (while
 * waiting, or after the match has stopped). */
void acr_rb_settle(acr_rb *rb);

/* Steps the caller should hold back to let the other side catch up (0 =
 * none). Skip at most one frame at a time. */
int acr_rb_should_wait(const acr_rb *rb);

/* Packets. write returns the length (0 on a too-small buffer); read returns
 * 1 for a valid packet from the other player, 2 for a goodbye, 0 for
 * anything else. */
int acr_rb_write(acr_rb *rb, uint8_t *buf, int cap);
int acr_rb_write_bye(uint8_t *buf, int cap);
int acr_rb_read(acr_rb *rb, const uint8_t *buf, int len);

/* The first step found out of step with the other machine, + 1 (0 = in
 * step so far). */
static inline uint32_t acr_rb_desync(const acr_rb *rb) { return rb->desync_frame; }

#endif
