/* Replays: a fight saved as the controls that were pressed, not as a film.
 *
 * The fight's rules give the same result from the same controls on every
 * machine (it is what rollback netplay relies on), so the pads of a fight
 * are all it takes to watch it again, from any camera. A replay is tiny:
 * a pad only costs bytes when it changes.
 *
 * What is in a replay:
 * - a header: this build's rules version, the data check of the parts and
 *   stage the fight was built from, the step length and the random seed;
 * - a setup block the game fills in and reads back (who fights whom and
 *   where: the builds, the stage, the mode, the names). Its bytes are not
 *   read in here;
 * - the pads, step by step, for 1 to 4 ACs: the players of a match, or
 *   every AC of a fight the game runs itself (a computer pilot's buttons
 *   are saved like a player's);
 * - a check every so often: the hash of the fight at that step. Playback
 *   compares them, so a replay that no longer plays out as it was recorded
 *   is found at the step where it goes wrong;
 * - an end record: how many steps, the last hash, who won.
 *
 * A replay only plays on the rules it was made with (ACR_REPLAY_RULES) and
 * the same data; anything else is refused with a reason instead of playing
 * out wrongly.
 *
 * No files, clocks or memory allocation in here: the caller gives the
 * recorder a buffer and saves it, and hands the reader the loaded bytes.
 * The bytes are written a record at a time and never changed afterwards,
 * so a replay can be saved while the fight is still going on; one that
 * stops without its end record (the game closed mid-fight) still plays as
 * far as it goes. Format: docs/REPLAYS.md. */
#ifndef ACR_REPLAY_H
#define ACR_REPLAY_H

#include <stdint.h>

#include "acr_gen1_match.h"
#include "acr_rollback.h"

#define ACR_REPLAY_FORMAT      1     /* the file layout */
/* The rules version: raise it with every change that makes the same
 * controls play out differently (tests/test_replay.c fails when one does
 * and says so). Replays made before the change are then refused. */
#define ACR_REPLAY_RULES       1
#define ACR_REPLAY_TRACKS      ACR_GEN1_MAX_FIGHTERS   /* most pads a step can hold */
#define ACR_REPLAY_SETUP_MAX   4096  /* largest setup block, bytes */
#define ACR_REPLAY_HEADER      32    /* bytes before the setup block */
#define ACR_REPLAY_CHECK_EVERY ACR_RB_HASH_EVERY       /* steps between checks (the online desync check's) */

/* What runs the fight being recorded. */
enum {
    ACR_REPLAY_MATCH  = 0,   /* acr_gen1_match_step: a track per player, 2 to 4 (VS, co-op, online) */
    ACR_REPLAY_BATTLE = 1    /* the game's own loop around acr_gen1_battle_step: a track per AC */
};

/* Why a replay can't be used. */
enum {
    ACR_REPLAY_OK = 0,
    ACR_REPLAY_NOT_A_REPLAY,   /* not one of our files (or far too short) */
    ACR_REPLAY_NEWER_FORMAT,   /* written by a newer build than this one */
    ACR_REPLAY_DAMAGED,        /* the bytes don't add up */
    ACR_REPLAY_OTHER_RULES,    /* made by a build whose fights play out differently */
    ACR_REPLAY_OTHER_DATA      /* made from different part or stage data */
};
/* A line for the screen. */
const char *acr_replay_error_text(int err);

typedef struct acr_replay_info {
    uint8_t  kind;          /* ACR_REPLAY_MATCH or ACR_REPLAY_BATTLE */
    uint8_t  tracks;        /* pads saved each step: 1..ACR_REPLAY_TRACKS */
    uint16_t step_ms;       /* the fight's step length */
    uint16_t check_every;   /* steps between checks (0: none); ACR_REPLAY_CHECK_EVERY unless there's a reason */
    uint32_t data_check;    /* the data the fight was built from (the online data check of the parts and the stage) */
    uint32_t seed;          /* the fight's random seed */
    uint32_t rules;         /* the build's ACR_REPLAY_RULES: set by the recorder, read back by the reader */
} acr_replay_info;

typedef struct acr_replay_end {
    uint32_t steps;         /* steps in the replay (the recorder counts them itself) */
    uint32_t hash;          /* the fight's hash after the last step */
    uint8_t  result;        /* 1 or 2: that side won; 3: a draw; 0: stopped before it was settled */
    uint8_t  rounds;        /* rounds started */
    uint8_t  wins[2];       /* by side */
} acr_replay_end;

/* ---------------- recording ---------------- */

typedef struct acr_replay_rec {
    uint8_t *buf;           /* the caller's buffer */
    uint32_t cap, len;      /* its size; bytes written to it so far */
    acr_replay_info info;
    acr_rb_pad pad[ACR_REPLAY_TRACKS];   /* each track's pad as last written */
    uint32_t run;           /* steps counted since the last record */
    uint32_t steps;         /* steps recorded */
    uint32_t checked;       /* the step the last check was written at, + 1 */
    uint32_t stream_hash;
    uint8_t  failed;        /* out of room (or an online match's pads were lost): nothing more is written */
    uint8_t  ended;
} acr_replay_rec;

/* Starts a replay in buf. `setup` (setup_len bytes, at most
 * ACR_REPLAY_SETUP_MAX) is the game's own description of the fight. Returns
 * 1, or 0 if it can't (bad info or a buffer too small for the header):
 * every later call is then ignored. */
int acr_replay_rec_begin(acr_replay_rec *rec, uint8_t *buf, uint32_t cap, const acr_replay_info *info,
                         const void *setup, uint32_t setup_len);

/* The pads the step about to run uses, one per track. */
void acr_replay_rec_step(acr_replay_rec *rec, const acr_rb_pad pads[]);

/* 1 when a check is due before the next step. */
int acr_replay_rec_wants_check(const acr_replay_rec *rec);
/* A check: the fight's hash after the steps recorded so far. */
void acr_replay_rec_check(acr_replay_rec *rec, uint32_t hash);

/* Closes the replay (end->steps is not read). Returns the bytes now in the
 * buffer, 0 if the replay failed. */
uint32_t acr_replay_rec_end(acr_replay_rec *rec, const acr_replay_end *end);

/* Saving while the fight goes on: after writing buf[0..len) to the file,
 * call this and the buffer is used again from its start. */
void acr_replay_rec_drop(acr_replay_rec *rec);

/* The most bytes a replay of that many steps can take (every pad changing
 * every step); a fight with held controls takes a small fraction of it. */
uint32_t acr_replay_worst_size(int tracks, uint32_t steps, uint32_t check_every, uint32_t setup_len);

/* A match (acr_gen1_match_step): call before each step with the players'
 * pads, one per track (track p is player p, who flies fighter p); writes
 * the check when one is due, then the step. */
void acr_replay_rec_match(acr_replay_rec *rec, const acr_gen1_match *m, const acr_rb_pad pads[]);
/* Closes it with the match as it stands. */
uint32_t acr_replay_rec_end_match(acr_replay_rec *rec, const acr_gen1_match *m);

/* An online match: call after every acr_rb_tick or acr_rb_settle. Rollback
 * can still change the latest steps, so this writes only the steps whose
 * pads are final on both sides (a few behind the screen), with the
 * session's own desync checks. Both machines end up with the same bytes. */
void acr_replay_rec_rollback(acr_replay_rec *rec, const acr_rb *rb);
/* Closes it at the last step both sides' pads are known for (settles the
 * session first). */
uint32_t acr_replay_rec_end_rollback(acr_replay_rec *rec, acr_rb *rb);

/* ---------------- playing back ---------------- */

typedef struct acr_replay_reader {
    const uint8_t *data;    /* the caller's bytes: must stay put while the reader is used */
    uint32_t first, last;   /* where the records start and stop */
    acr_replay_info info;
    const uint8_t *setup;   /* the game's setup block */
    uint32_t setup_len;
    uint32_t steps;         /* steps in the replay */
    uint8_t  complete;      /* 1: it has its end record. 0: cut off (the game closed mid-fight); plays as far as it goes */
    acr_replay_end end;     /* the end record (complete only) */
    /* Where playback is. Plain values: copy the reader along with the fight
     * to come back to that moment later (rewinding, jumping). */
    uint32_t pos, run, step;
    acr_rb_pad pad[ACR_REPLAY_TRACKS];
    uint32_t mismatch;      /* matches: the step + 1 of the first check that differed (0: playing out as recorded) */
} acr_replay_reader;

/* Reads a loaded replay and checks it through, end to end. Returns
 * ACR_REPLAY_OK or why not. */
int acr_replay_open(acr_replay_reader *rd, const uint8_t *data, uint32_t len);

/* Can this build, with this data, play it? ACR_REPLAY_OK, _OTHER_RULES or
 * _OTHER_DATA. */
int acr_replay_fits(const acr_replay_reader *rd, uint32_t data_check);

/* Back to the first step. */
void acr_replay_rewind(acr_replay_reader *rd);

enum { ACR_REPLAY_EV_END = 0, ACR_REPLAY_EV_STEP = 1, ACR_REPLAY_EV_CHECK = 2 };
/* The next thing in the replay:
 * - ACR_REPLAY_EV_STEP: pads[] (one per track) are the next step's; run it.
 * - ACR_REPLAY_EV_CHECK: *hash is what the fight's hash should be now.
 * - ACR_REPLAY_EV_END: no more steps. */
int acr_replay_next(acr_replay_reader *rd, acr_rb_pad pads[], uint32_t *hash);

/* A match replay, a step at a time (for watching): runs the next step on m
 * and compares the checks on the way. Returns 1 if a step ran, 0 at the end
 * (the end record's hash is compared too). rd->mismatch tells whether the
 * fight still plays out as recorded. Start from acr_replay_rewind and
 * acr_gen1_match_init with the fight the setup block describes. */
int acr_replay_match_step(acr_replay_reader *rd, acr_gen1_match *m, const acr_gen1_battle *start,
                          const acr_gen1_match_rules *rules);

/* The whole match replay in one go (tests, checking a file): leaves m at
 * the end. Returns 0 if every check matched, else the step + 1 of the first
 * one that didn't. */
uint32_t acr_replay_play_match(acr_replay_reader *rd, acr_gen1_match *m, const acr_gen1_battle *start,
                               const acr_gen1_match_rules *rules);

#endif
