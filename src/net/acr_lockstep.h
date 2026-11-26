/* Lockstep netplay: 2 to 4 machines, one player each, for LAN (and later
 * the Xbox's System Link).
 *
 * Every machine runs a step only once it has every player's controls for
 * it, so all of them always show the same fight and nothing is ever
 * corrected afterwards. To keep that from waiting on the network, your own
 * controls take effect `delay` steps after you press them: by then they
 * have reached the others. Too little delay for the connection and the
 * game pauses for a moment whenever a packet is late; rollback
 * (acr_rollback.h) is the mode for connections like that.
 *
 * Against rollback: no snapshots and no steps run twice (light enough for
 * the Xbox), and more than two players. The price is the fixed delay on
 * your own controls.
 *
 * - Player p flies fighter p. Player 0 is the host; every packet goes
 *   through it (guests talk only to the host, which passes each guest's
 *   controls on to the others), so a guest needs one connection.
 * - Controls go out as 3-byte pads; each packet repeats what the other
 *   machine hasn't confirmed yet, so a lost packet costs nothing but time.
 * - The machine running ahead holds back a step now and then
 *   (acr_ls_should_wait) so nobody sits waiting at the edge.
 * - Desync check: every ACR_LS_HASH_EVERY steps each machine sends a hash
 *   of the match; a mismatch is reported (acr_ls_desync).
 *
 * No sockets or clocks in here: the caller moves the packets and calls
 * acr_ls_tick once a step. */
#ifndef ACR_LOCKSTEP_H
#define ACR_LOCKSTEP_H

#include <stdint.h>

#include "acr_gen1_match.h"
#include "acr_rollback.h"   /* the pad: acr_rb_pad, acr_rb_pack, acr_rb_unpack */

#define ACR_LS_PLAYERS     ACR_GEN1_MAX_FIGHTERS
#define ACR_LS_RING        128   /* pads kept per player */
#define ACR_LS_AHEAD       48    /* pads taken this far past our own step */
#define ACR_LS_MAX_DELAY   15
#define ACR_LS_HASH_EVERY  60    /* steps between desync checks (1 s at 60) */
#define ACR_LS_HASHES      8
#define ACR_LS_PADS_MAX    32    /* pads of one player in one packet */
#define ACR_LS_PACKET_MAX  352

typedef struct acr_ls {
    int players;                          /* machines in the match: 2..ACR_LS_PLAYERS */
    int local;                            /* our player; 0 is the host */
    int delay;                            /* steps between pressing and taking effect: 1..ACR_LS_MAX_DELAY */
    const acr_gen1_battle *start;
    acr_gen1_match_rules rules;
    acr_gen1_match cur;                   /* the match */
    uint32_t frame;                       /* the next step to run (= cur.frame) */

    acr_rb_pad pad[ACR_LS_PLAYERS][ACR_LS_RING];       /* each player's pad by step */
    uint32_t   pad_for[ACR_LS_PLAYERS][ACR_LS_RING];   /* the step + 1 each slot holds (0 = empty) */
    uint32_t   next[ACR_LS_PLAYERS];                   /* each player's first step we have no pad for */
    /* peer_next[q][p]: machine q's first missing step of player p, as it
     * last told us (what to send it next). */
    uint32_t   peer_next[ACR_LS_PLAYERS][ACR_LS_PLAYERS];
    uint32_t   peer_frame[ACR_LS_PLAYERS];             /* each machine's step, as it last told us */
    int32_t    local_adv256[ACR_LS_PLAYERS], remote_adv256[ACR_LS_PLAYERS];   /* frame advantage, averaged, 1/256 steps */
    uint8_t    gone[ACR_LS_PLAYERS];                   /* said goodbye */
    uint8_t    waiting_for;                            /* bit p: the last tick waited for player p's pad */

    /* Desync checks */
    uint32_t next_hash;
    uint32_t hash_at[ACR_LS_HASHES], hash_mine[ACR_LS_HASHES];
    uint8_t  have_mine[ACR_LS_HASHES];
    uint32_t hash_theirs[ACR_LS_PLAYERS][ACR_LS_HASHES];
    uint8_t  have_theirs[ACR_LS_PLAYERS][ACR_LS_HASHES], checked[ACR_LS_PLAYERS][ACR_LS_HASHES];
    uint32_t out_hash_frame, out_hash;    /* the latest hash we send */
    uint32_t desync_frame;                /* first step found out of step + 1 (0 = fine) */
    uint8_t  desync_with;                 /* the machine it was found against */
    uint32_t checks_ok;                   /* hashes that matched */

    /* Stats for the screen and the tests */
    uint32_t waits, packets_in, packets_bad;
} acr_ls;

/* A new session. Every machine must pass the same players, start, rules
 * and delay; `local` is this machine's player (0 = the host). */
void acr_ls_init(acr_ls *ls, int players, int local, int delay, const acr_gen1_battle *start,
                 const acr_gen1_match_rules *rules);

/* 1 if the next acr_ls_tick will use the pad it is given (it takes one pad
 * per step run, so not while it waits): the caller then forgets its
 * collected button presses. */
int acr_ls_wants_pad(const acr_ls *ls);

/* One step with our controls. Returns 1 if it ran, 0 if it must wait for
 * someone's pad (ls->waiting_for says whose); call again next frame. */
int acr_ls_tick(acr_ls *ls, acr_rb_pad local_pad);

/* Steps the caller should hold back to let the others catch up (0 = none).
 * Skip at most one frame at a time. */
int acr_ls_should_wait(const acr_ls *ls);

/* Packets. A guest writes to the host (to = 0) only; the host writes to
 * every guest. write returns the length (0: not a machine we send to, or a
 * buffer under ACR_LS_PACKET_MAX). read returns 1 for a valid packet, 2 for
 * a goodbye (ls->gone), 0 for anything else. */
int acr_ls_write(acr_ls *ls, int to, uint8_t *buf, int cap);
int acr_ls_write_bye(const acr_ls *ls, uint8_t *buf, int cap);
int acr_ls_read(acr_ls *ls, const uint8_t *buf, int len);

/* The pads step f ran (or will run) with, one per player, for replays
 * (acr_replay_rec_step right after a tick: f = ls->frame - 1). Returns 0
 * if they aren't held any more. */
int acr_ls_pads(const acr_ls *ls, uint32_t f, acr_rb_pad pads[]);

/* The first step found out of step with another machine, + 1 (0 = in step
 * so far). */
static inline uint32_t acr_ls_desync(const acr_ls *ls) { return ls->desync_frame; }

#endif
