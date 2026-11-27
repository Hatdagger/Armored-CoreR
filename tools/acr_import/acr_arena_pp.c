/* Project Phantasma's arena -> one readable TOML file per opponent. See
 * acr_arena_pp.h for where it lives on the disc. */
#include "acr_arena_pp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define acr_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define acr_mkdir(p) mkdir((p), 0755)
#endif

#define PLACES      50u     /* 49 opponents + the player */
#define E_EMBLEM    282u
#define E_BUILDS    382u
#define E_NAMES     482u
#define E_REWARDS   483u
#define AI_OFFSET   0x453C8u  /* FDAT 205 (arena engine), RAM 0x8008E498 */
#define AI_BYTES    60u
#define BUILD_BYTES 64u
#define NAME_BYTES  24u

static void set_err(char *err, size_t n, const char *what, const char *path) {
    if (err && n) snprintf(err, n, "%s%s%s", what, path ? ": " : "", path ? path : "");
}

static int make_dirs(const char *path) {
    char buf[1024];
    const size_t len = strlen(path);
    if (len >= sizeof buf) return 0;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i <= len; ++i) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            const char saved = buf[i];
            buf[i] = '\0';
            if (acr_mkdir(buf) != 0 && errno != EEXIST) return 0;
            buf[i] = saved;
        }
    }
    return 1;
}

static uint8_t *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    *size = d ? (size_t)n : 0;
    return d;
}

static uint8_t *read_entry(const char *unpacked, unsigned k, size_t *size) {
    char path[1200];
    snprintf(path, sizeof path, "%s/GG/MS/MENU_TIM/%03u.bin", unpacked, k);
    return read_all(path, size);
}

/* A name as a TOML string body. Phantasma ends names with '>'; four
 * "Panzern" ACs carry a symbol from the game's own font (bytes 0xA2-0xA5),
 * written here as the numerals IV-VII (0xA5 is rank 5's Panzern VII, 0xA2
 * rank 17's Panzern IV). */
static void clean_name(const uint8_t *p, size_t n, char *out, size_t size) {
    static const char *const k_mark[4] = { "IV", "V", "VI", "VII" };
    size_t o = 0;
    for (size_t i = 0; i < n && p[i] && p[i] != '>' && o + 1 < size; ++i) {
        if (p[i] >= 0xA2 && p[i] <= 0xA5) {
            for (const char *m = k_mark[p[i] - 0xA2]; *m && o + 1 < size; ++m) out[o++] = *m;
        } else if (p[i] >= 0x20 && p[i] < 0x7F && p[i] != '"' && p[i] != '\\') {
            out[o++] = (char)p[i];
        }
    }
    while (o && out[o - 1] == ' ') --o;
    out[o] = '\0';
}

static int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

/* Phantasma back weapon -> Master of Arena's number (see the header). */
static int back_id(int v) { return v == 255 ? 255 : v >= 17 ? v + 1 : v; }

static int write_opponent(const char *path, unsigned rank, const uint8_t *b, const char *pilot, const char *ac,
                          const uint8_t *rw, const uint8_t *ai) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    const int hand = (b[4] == 0 || b[4] == 255) ? 255 : b[4];
    const unsigned options = (unsigned)(b[10] | (b[11] & 0x0F) << 8);
    const uint32_t money = (uint32_t)(rw[0] | rw[1] << 8 | rw[2] << 16 | (uint32_t)rw[3] << 24);
    const unsigned prize = (unsigned)(rw[4] | rw[5] << 8);
    /* The AI editor rows from Phantasma's record (ai.md section 3): fly =
     * desire C (air, +3), blade = the lunge chance (+9), lock from the
     * reaction delay (+0x11, frames: 0 = 100, 25 or more = 0). Phantasma has
     * no fire-chance, dodge or range fields: those rows get our defaults. */
    const int fly = clamp100(ai[3]), blade = clamp100(ai[9]), lock = clamp100(100 - 4 * ai[0x11]);
    fprintf(f, "# Project Phantasma arena opponent, imported from your disc by acr-import.\n");
    fprintf(f, "pilot      = \"%s\"\n", pilot);
    fprintf(f, "ac         = \"%s\"\n", ac);
    fprintf(f, "arena      = %u   # Armored-CoreR's main Arena (Project Phantasma's)\n", (unsigned)ACR_ARENA_PP_NUMBER);
    fprintf(f, "arena_name = \"ARENA\"\n");
    fprintf(f, "source     = \"phantasma\"\n");
    fprintf(f, "rank       = %u   # 1 = the arena's top\n", rank);
    fprintf(f, "entries    = %u   # opponents in this arena, the player's place included\n", PLACES);
    fprintf(f, "core       = %d\nhead       = %d\narms       = %d\nlegs       = %d\n", b[0], b[1], b[2], b[3]);
    fprintf(f, "hand       = %d\nblade      = %d\nback_l     = %d\nback_r     = %d   # Master of Arena's numbers\n",
            hand, b[5], back_id(b[6]), back_id(b[7]));
    fprintf(f, "fcs        = %d\ngenerator  = %d\nbooster    = %d   # 255 = none\n", b[8], b[9], b[12]);
    fprintf(f, "options    = %u   # bit n = option part n\n", options);
    fprintf(f, "colours    = [%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d]   # 4 colours, red green blue 0..63\n",
            b[16], b[17], b[18], b[19], b[20], b[21], b[22], b[23], b[24], b[25], b[26], b[27]);
    fprintf(f, "prize_money = %u\n", (unsigned)money);
    fprintf(f, "prize_part = %d   # Armored-CoreR prize id (23-31 = Phantasma's 0-8), -1 none\n",
            prize == 0xFFFFu || prize > 8 ? -1 : (int)(ACR_ARENA_PP_PRIZE_BASE + prize));
    fprintf(f, "# AI: the AI editor rows, read from Phantasma's arena AI record %u (rank - 1, a guess).\n", rank - 1);
    fprintf(f, "rebel = %d   # Human PLUS level\n", ai[0] > 9 ? 9 : ai[0]);
    fprintf(f, "shoot = 75\nblade_use = %d\nlock = %d\navoid = 50\nfly = %d\nrange = 50\n", blade, lock, fly);
    fprintf(f, "ai_record_pp = [");
    for (unsigned i = 0; i < AI_BYTES; ++i) fprintf(f, "%s%u", i ? ", " : "", ai[i]);
    fprintf(f, "]\n");
    return fclose(f) == 0;
}

int acr_export_pp_arena(const char *pp_unpacked, const char *out_dir, uint32_t *count, char *err, size_t err_size) {
    if (count) *count = 0;
    size_t nb = 0, nn = 0, nr = 0, na = 0;
    char path[1400];
    snprintf(path, sizeof path, "%s/GG/COM/FDAT/205.bin", pp_unpacked);
    uint8_t *eng = read_all(path, &na);
    uint8_t *bld = read_entry(pp_unpacked, E_BUILDS, &nb), *nam = read_entry(pp_unpacked, E_NAMES, &nn);
    uint8_t *rew = read_entry(pp_unpacked, E_REWARDS, &nr);
    int ok = 1;
    if (!eng || !bld || !nam || !rew || nb < PLACES * BUILD_BYTES || nn < 2 * (PLACES - 1) * NAME_BYTES ||
        nr < PLACES * 8 || na < AI_OFFSET + (PLACES - 1) * AI_BYTES) {
        set_err(err, err_size, "the Project Phantasma disc's arena data could not be read", pp_unpacked);
        ok = 0;
    }
    char dir[1300];
    snprintf(dir, sizeof dir, "%s/%s", out_dir, ACR_ARENA_PP_FOLDER);
    if (ok && !make_dirs(dir)) { set_err(err, err_size, "cannot create", dir); ok = 0; }
    unsigned written = 0;
    for (unsigned r = 0; ok && r + 1 < PLACES; ++r) {
        const uint8_t *b = bld + r * BUILD_BYTES;
        if (b[0] == 0xFF || b[1] == 0xFF) continue;
        char pilot[40], ac[40];
        /* pilot and AC alternate: rank 1 Necron / Black Dragon, rank 2 ... */
        clean_name(nam + (2 * r) * NAME_BYTES, NAME_BYTES, pilot, sizeof pilot);
        clean_name(nam + (2 * r + 1) * NAME_BYTES, NAME_BYTES, ac, sizeof ac);
        snprintf(path, sizeof path, "%s/%02u.toml", dir, r + 1);
        if (!write_opponent(path, r + 1, b, pilot, ac, rew + r * 8, eng + AI_OFFSET + r * AI_BYTES)) {
            set_err(err, err_size, "cannot write", path);
            ok = 0;
            break;
        }
        ++written;
        size_t ne = 0;
        uint8_t *em = read_entry(pp_unpacked, E_EMBLEM + r, &ne);
        if (em && ne >= 20) {
            snprintf(path, sizeof path, "%s/%02u-emblem.tim", dir, r + 1);
            FILE *ef = fopen(path, "wb");
            if (ef) { fwrite(em, 1, ne, ef); fclose(ef); }
        }
        free(em);
    }
    free(eng); free(bld); free(nam); free(rew);
    if (ok && count) *count = written;
    return ok;
}
