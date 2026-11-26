/* Builds a tiny raw MODE2/2352 PS1 disc with a MoA-style .T archive and an XA
 * file, imports it, and checks what lands on disk. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acr_import.h"
#include "acr_sfx_moa.h"
#include "acr_stages_moa.h"

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir((p), 0755)
#endif

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

#define SECTORS 27
static unsigned char user[SECTORS][2048];
static int form2[SECTORS];

static void put32(unsigned char *p, unsigned v) {   /* ISO both-endian 32 */
    p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = v >> 24;
    p[4] = v >> 24; p[5] = (v >> 16) & 255; p[6] = (v >> 8) & 255; p[7] = v & 255;
}

static int dir_record(unsigned char *p, const char *name, int name_len, unsigned lba,
                      unsigned bytes, int is_dir) {
    const int len = 33 + name_len + ((name_len & 1) ? 0 : 1);
    memset(p, 0, (size_t)len);
    p[0] = (unsigned char)len;
    put32(p + 2, lba);
    put32(p + 10, bytes);
    p[25] = is_dir ? 2 : 0;
    p[32] = (unsigned char)name_len;
    memcpy(p + 33, name, (size_t)name_len);
    return len;
}

static void build_disc(void) {
    memset(user, 0, sizeof user);
    /* 16: primary volume descriptor, root directory at 18. */
    user[16][0] = 1; memcpy(user[16] + 1, "CD001", 5); user[16][6] = 1;
    dir_record(user[16] + 156, "\0", 1, 18, 2048, 1);
    user[17][0] = 255; memcpy(user[17] + 1, "CD001", 5);
    /* 18: root = . .. MS/ README.TXT MUSIC.XA */
    int at = 0;
    at += dir_record(user[18] + at, "\0", 1, 18, 2048, 1);
    at += dir_record(user[18] + at, "\1", 1, 18, 2048, 1);
    at += dir_record(user[18] + at, "MS", 2, 19, 2048, 1);
    at += dir_record(user[18] + at, "README.TXT;1", 12, 20, 5, 0);
    dir_record(user[18] + at, "MUSIC.XA;1", 10, 25, 2 * 2048, 0);
    /* 19: MS = . .. TEST.T */
    at = 0;
    at += dir_record(user[19] + at, "\0", 1, 19, 2048, 1);
    at += dir_record(user[19] + at, "\1", 1, 18, 2048, 1);
    dir_record(user[19] + at, "TEST.T;1", 8, 21, 4 * 2048, 0);
    memcpy(user[20], "hello", 5);
    /* 21-24: archive, 2 entries: sector 1 (1 sector) and sectors 2-3. */
    user[21][0] = 2; user[21][2] = 1; user[21][4] = 2; user[21][6] = 4;
    memset(user[22], 0xA1, 2048);
    memset(user[23], 0xB2, 2048);
    memset(user[24], 0xB3, 2048);
    /* 25-26: XA audio (Form 2). */
    memset(user[25], 0x55, 2048); form2[25] = 1;
    memset(user[26], 0x66, 2048); form2[26] = 1;
}

static int write_bin_cue(const char *dir) {
    char path[512];
    snprintf(path, sizeof path, "%s/test.bin", dir);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    for (int s = 0; s < SECTORS; ++s) {
        unsigned char raw[2352] = {0};
        memset(raw + 1, 0xFF, 10);
        const int abs = s + 150;
        raw[12] = (unsigned char)(((abs / 4500) / 10) << 4 | ((abs / 4500) % 10));
        raw[13] = (unsigned char)((((abs / 75) % 60) / 10) << 4 | (((abs / 75) % 60) % 10));
        raw[14] = (unsigned char)(((abs % 75) / 10) << 4 | ((abs % 75) % 10));
        raw[15] = 2;
        raw[18] = raw[22] = form2[s] ? 0x24 : 0x08;
        memcpy(raw + 24, user[s], 2048);
        if (fwrite(raw, 1, sizeof raw, f) != sizeof raw) { fclose(f); return 0; }
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/test.cue", dir);
    f = fopen(path, "wb");
    if (!f) return 0;
    fputs("FILE \"test.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n", f);
    fclose(f);
    return 1;
}

static long file_size(const char *path, unsigned char *first) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (first) *first = (unsigned char)fgetc(f);
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fclose(f);
    return n;
}

/* Stage lights: the GTE's NCCS worked by hand for a made-up light set. */
static void test_stage_light(void) {
    const int16_t llm[9] = { 0, -4096, 0, 4096, 0, 0, 0, 0, 0 };
    const int16_t lcm[9] = { 4096, 2048, 0, 4096, 2048, 0, 4096, 2048, 0 };
    const uint8_t bk[3] = { 255, 0, 128 };
    const int16_t up[3] = { 0, -4096, 0 }, east[3] = { 4096, 0, 0 }, south[3] = { 0, 0, -4096 };
    uint8_t c[3];
    acr_moa_light_face(llm, lcm, bk, 0, up, c);       /* light 0 full: 4080 + 4096 */
    CHECK(c[0] == 127 && c[1] == 64 && c[2] == 96);
    acr_moa_light_face(llm, lcm, bk, 0, east, c);     /* light 1 full, at half colour */
    CHECK(c[0] == 95 && c[1] == 32 && c[2] == 64);
    acr_moa_light_face(llm, lcm, bk, 0, south, c);    /* back colour only */
    CHECK(c[0] == 63 && c[1] == 0 && c[2] == 32);
    acr_moa_light_face(llm, lcm, bk, 1, south, c);    /* a quarter-turned area: light 1 turned with it */
    CHECK(c[0] == 95 && c[1] == 32 && c[2] == 64);
    acr_moa_light_face(llm, lcm, bk, 1, east, c);
    CHECK(c[0] == 63 && c[1] == 0 && c[2] == 32);
}

int main(int argc, char **argv) {
    const char *work = argc > 1 ? argv[1] : "test_import_work";
    test_stage_light();
    make_dir(work);
    build_disc();
    CHECK(write_bin_cue(work));

    char cue[512], out[512], path[640];
    snprintf(cue, sizeof cue, "%s/test.cue", work);
    snprintf(out, sizeof out, "%s/out", work);
    acr_import_stats st;
    char err[256] = "";
    CHECK(acr_import_disc(cue, out, &st, err, sizeof err));
    if (err[0]) fprintf(stderr, "import error: %s\n", err);
    CHECK(st.files == 2 && st.raw_files == 1 && st.archives == 1 && st.archive_entries == 2);

    unsigned char b = 0;
    snprintf(path, sizeof path, "%s/disc/README.TXT", out);
    CHECK(file_size(path, &b) == 5 && b == 'h');
    snprintf(path, sizeof path, "%s/disc/MS/TEST.T", out);
    CHECK(file_size(path, NULL) == 4 * 2048);
    snprintf(path, sizeof path, "%s/unpacked/MS/TEST/000.bin", out);
    CHECK(file_size(path, &b) == 2048 && b == 0xA1);
    snprintf(path, sizeof path, "%s/unpacked/MS/TEST/001.bin", out);
    CHECK(file_size(path, &b) == 2 * 2048 && b == 0xB2);
    snprintf(path, sizeof path, "%s/unpacked/MS/TEST/archive.toml", out);
    CHECK(file_size(path, NULL) > 0);
    snprintf(path, sizeof path, "%s/disc/MUSIC.XA.raw2352", out);
    CHECK(file_size(path, &b) == 2 * 2352 && b == 0x00);

    /* Archive tables that do not check out are left alone. */
    unsigned char bad[2048] = {0};
    unsigned short off[8];
    bad[0] = 2; bad[2] = 0; bad[4] = 1; bad[6] = 2;     /* entry 0 starts on the table */
    CHECK(acr_moa_archive_table(bad, sizeof bad, off, 8) == 0);
    bad[2] = 1; bad[4] = 3; bad[6] = 2;                 /* goes backwards */
    CHECK(acr_moa_archive_table(bad, sizeof bad, off, 8) == 0);

    /* Sound samples: PS1 ADPCM, 28 frames a 16-byte block, block 0 a silent
     * lead-in; flag 1 ends, 2 repeats, 4 marks the loop start. */
    unsigned char vag[48] = {0};
    int16_t pcm[96];
    long loop = 0;
    vag[16] = 12; vag[17] = 1; vag[18] = 0x71;           /* shift 12, filter 0, end: 1 then 7 */
    CHECK(acr_moa_vag_decode(vag, 32, pcm, 96, &loop) == 56);
    CHECK(pcm[0] == 0 && pcm[28] == 1 && pcm[29] == 7 && pcm[30] == 0 && loop == -1);
    vag[17] = 6; vag[33] = 3;                            /* loop from block 1, end at block 2 */
    CHECK(acr_moa_vag_decode(vag, 48, pcm, 96, &loop) == 84 && loop == 28);

    if (failures) return 1;
    printf("import: all checks passed\n");
    return 0;
}
