/* Master of Arena arena opponents -> one readable TOML file each. See
 * acr_arena_moa.h for where they live on the disc. */
#include "acr_arena_moa.h"

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

#define MENU_BASE      0x80050DD8u
#define ARENA_COUNTS   0x800D5A08u   /* opponents per arena (byte each) */
#define ARENA_STARTS   0x800D5A14u   /* opponents in earlier arenas */
#define ARENAS         9u            /* 0 main, 1-8 Ex Arena (9 Custom is player-made) */
#define FIRST_ENTRY    140u

#define BUILD_BYTES 64u
#define NAME_BYTES  48u
#define PRIZE_BYTES 8u
#define AI_BYTES    36u

static const char *const k_arena_name[ARENAS] = {
    "ARENA", "HUMANOID", "FOUR LEGS", "REVERSE JOINT", "CATERPILLAR",
    "CHAMPION", "GUEST", "FROMSOFT", "MASTER",
};
static const char *const k_arena_slug[ARENAS] = {
    "arena", "humanoid", "four-legs", "reverse-joint", "caterpillar",
    "champion", "guest", "fromsoft", "master",
};

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

/* A name as a TOML string body: printable ASCII, quotes and backslashes dropped. */
static void clean_name(const uint8_t *p, size_t n, char *out, size_t size) {
    size_t o = 0;
    for (size_t i = 0; i < n && p[i] && o + 1 < size; ++i)
        if (p[i] >= 0x20 && p[i] < 0x7F && p[i] != '"' && p[i] != '\\') out[o++] = (char)p[i];
    while (o && out[o - 1] == ' ') --o;
    out[o] = '\0';
}

static int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

/* The AI editor's sliders back from a record (the inverse of what the
 * editor writes, acr_gen1_ai_record). */
static void ai_sliders(const uint8_t *a, int s[6]) {
    s[0] = clamp100(a[13] - 1);                     /* shoot */
    s[1] = clamp100(a[9]);                          /* blade */
    s[2] = a[11] == 0 ? 100 : clamp100(a[11] - 1);  /* lock */
    s[3] = clamp100(a[14]);                         /* avoid */
    s[4] = clamp100(a[3]);                          /* fly */
    s[5] = clamp100(a[24]);                         /* range */
}

static int write_opponent(const char *path, unsigned arena, unsigned rank, unsigned entries,
                          const uint8_t *b, const uint8_t *nm, const uint8_t *pz, const uint8_t *ai) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    char pilot[32], ac[32];
    clean_name(nm, 24, pilot, sizeof pilot);
    clean_name(nm + 24, 24, ac, sizeof ac);
    /* Record order (the game's): core, head, arms, legs, hand, blade, back
     * left, back right, FCS, generator, option bits (2 bytes), booster. 255 =
     * none; hand 0 is the table's NO WEAPON row, also none. */
    const int hand = (b[4] == 0 || b[4] == 255) ? 255 : b[4];
    const unsigned options = (unsigned)(b[10] | (b[11] & 0x0F) << 8);
    const uint32_t money = (uint32_t)(pz[0] | pz[1] << 8 | pz[2] << 16 | (uint32_t)pz[3] << 24);
    const unsigned prize_part = (unsigned)(pz[4] | pz[5] << 8);
    int s[6];
    ai_sliders(ai, s);
    fprintf(f, "# Master of Arena arena opponent, imported from your disc by acr-import.\n");
    fprintf(f, "pilot      = \"%s\"\n", pilot);
    fprintf(f, "ac         = \"%s\"\n", ac);
    fprintf(f, "arena      = %u   # 0 the main arena, 1-8 the Ex Arenas\n", arena);
    fprintf(f, "arena_name = \"%s\"\n", k_arena_name[arena]);
    fprintf(f, "rank       = %u   # 1 = the arena's top\n", rank);
    fprintf(f, "entries    = %u   # opponents in this arena, the player's place included\n", entries);
    fprintf(f, "core       = %d\nhead       = %d\narms       = %d\nlegs       = %d\n", b[0], b[1], b[2], b[3]);
    fprintf(f, "hand       = %d\nblade      = %d\nback_l     = %d\nback_r     = %d\n", hand, b[5], b[6], b[7]);
    fprintf(f, "fcs        = %d\ngenerator  = %d\nbooster    = %d   # 255 = none\n", b[8], b[9], b[12]);
    fprintf(f, "options    = %u   # bit n = option part n\n", options);
    fprintf(f, "colours    = [%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d]   # 4 colours, red green blue 0..63\n",
            b[16], b[17], b[18], b[19], b[20], b[21], b[22], b[23], b[24], b[25], b[26], b[27]);
    fprintf(f, "prize_money = %u\n", (unsigned)money);
    fprintf(f, "prize_part = %d   # the game's prize id (arena-prizes.md), -1 none\n",
            prize_part == 0xFFFFu ? -1 : (int)prize_part);
    fprintf(f, "# AI: the game's AI editor rows (docs/AI.md), read back from its record.\n");
    fprintf(f, "rebel = %d   # Human PLUS level\n", ai[0] > 9 ? 9 : ai[0]);
    fprintf(f, "shoot = %d\nblade_use = %d\nlock = %d\navoid = %d\nfly = %d\nrange = %d\n",
            s[0], s[1], s[2], s[3], s[4], s[5]);
    fprintf(f, "ai_record = [");
    for (unsigned i = 0; i < AI_BYTES; ++i) fprintf(f, "%s%u", i ? ", " : "", ai[i]);
    fprintf(f, "]\n");
    return fclose(f) == 0;
}

int acr_export_moa_arena(const char *menu_module, const char *unpacked_dir, const char *out_dir,
                         uint32_t *count, char *err, size_t err_size) {
    if (count) *count = 0;
    size_t msize = 0;
    uint8_t *m = read_all(menu_module, &msize);
    const uint32_t oc = ARENA_COUNTS - MENU_BASE, os = ARENA_STARTS - MENU_BASE;
    if (!m || msize < os + ARENAS) {
        free(m);
        set_err(err, err_size, "the menu module could not be read", menu_module);
        return 0;
    }
    if (!make_dirs(out_dir)) { free(m); set_err(err, err_size, "cannot create", out_dir); return 0; }
    char path[1400];
    snprintf(path, sizeof path, "%s/arenas.toml", out_dir);
    FILE *list = fopen(path, "w");
    if (!list) { free(m); set_err(err, err_size, "cannot write", path); return 0; }
    fprintf(list, "# Master of Arena's arenas, imported from your disc by acr-import.\n");
    int ok = 1;
    for (unsigned a = 0; a < ARENAS && ok; ++a) {
        const unsigned cnt = m[oc + a], start = m[os + a];
        if (!cnt || cnt > 64) continue;
        const unsigned k0 = FIRST_ENTRY + 4u * start + 4u * a, k = k0 + 4u * cnt;
        size_t nb = 0, nn = 0, np = 0, na = 0;
        uint8_t *bld = read_entry(unpacked_dir, k, &nb), *nam = read_entry(unpacked_dir, k + 1, &nn);
        uint8_t *prz = read_entry(unpacked_dir, k + 2, &np), *ais = read_entry(unpacked_dir, k + 3, &na);
        if (!bld || !nam || !prz || !ais || nb < cnt * BUILD_BYTES || nn < cnt * NAME_BYTES ||
            np < cnt * PRIZE_BYTES || na < cnt * AI_BYTES) {
            set_err(err, err_size, "an arena's opponent data could not be read", k_arena_name[a]);
            ok = 0;
        }
        char dir[1300];
        snprintf(dir, sizeof dir, "%s/%02u-%s", out_dir, a, k_arena_slug[a]);
        if (ok && !make_dirs(dir)) { set_err(err, err_size, "cannot create", dir); ok = 0; }
        unsigned written = 0;
        /* The last entry is the player's own place in that arena. */
        for (unsigned r = 0; ok && r + 1 < cnt; ++r) {
            const uint8_t *b = bld + r * BUILD_BYTES;
            if (b[0] == 0xFF || b[1] == 0xFF) continue;   /* an unused slot has no core */
            snprintf(path, sizeof path, "%s/%02u.toml", dir, r + 1);
            if (!write_opponent(path, a, r + 1, cnt, b, nam + r * NAME_BYTES, prz + r * PRIZE_BYTES,
                                ais + r * AI_BYTES)) {
                set_err(err, err_size, "cannot write", path);
                ok = 0;
            }
            ++written;
            /* Its emblem: the 4-bit 64 x 64 TIM as on the disc (the entries
             * after the descriptions, one per opponent). */
            size_t ne = 0;
            uint8_t *em = read_entry(unpacked_dir, k0 + cnt + r, &ne);
            if (em && ne >= 20) {
                snprintf(path, sizeof path, "%s/%02u-emblem.tim", dir, r + 1);
                FILE *ef = fopen(path, "wb");
                if (ef) { fwrite(em, 1, ne, ef); fclose(ef); }
            }
            free(em);
        }
        if (ok) {
            fprintf(list, "[[arena]]\nnumber = %u\nname = \"%s\"\nfolder = \"%02u-%s\"\nentries = %u\nopponents = %u\n",
                    a, k_arena_name[a], a, k_arena_slug[a], cnt, written);
            if (count) *count += written;
        }
        free(bld); free(nam); free(prz); free(ais);
    }
    if (fclose(list) != 0 && ok) { set_err(err, err_size, "cannot write", path); ok = 0; }
    free(m);
    return ok;
}
