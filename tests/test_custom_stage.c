/* A map made in Tinkercad and turned into a stage by
 * tools/maps/tinkercad_stage.py (docs/MAPS.md). Run by the custom_stage
 * test (tests/custom_stage.cmake) on tools/maps/make_sample.py's sample
 * map and on its three joined tiles (A1, B1, "A2 rot90"); with
 * ACR_CUSTOM_STAGE set to any converted stage's folder it checks that one's
 * starts instead. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acr_gen1_stage.h"
#include "acr_model.h"
#include "acr_png.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static char *read_text(const char *dir, const char *name) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *t = malloc((size_t)n + 1);
    if (t && fread(t, 1, (size_t)n, f) != (size_t)n) { free(t); t = NULL; }
    if (t) t[n] = 0;
    fclose(f);
    return t;
}

static int start(const char *toml, const char *key, int32_t out[4]) {
    const char *p = strstr(toml, key);
    if (!p || !(p = strchr(p, '['))) return 0;
    return sscanf(p, "[%d, %d, %d, %d]", &out[0], &out[1], &out[2], &out[3]) == 4;
}

int main(int argc, char **argv) {
    const char *dir = getenv("ACR_CUSTOM_STAGE");
    const int sample = !dir;
    if (!dir) dir = argc > 1 ? argv[1] : NULL;
    if (!dir) { printf("custom stage: no stage folder given, skipped\n"); return 0; }

    char *toml = read_text(dir, "stage.toml"), *coll = read_text(dir, "collision.obj");
    char *vis = read_text(dir, "visual.obj"), *mtl = read_text(dir, "visual.mtl");
    EXPECT(toml && coll && vis && mtl);
    if (!toml || !coll || !vis || !mtl) return 1;

    /* Drawn the way the game draws stages. */
    acr_mtl m;
    acr_mesh mesh;
    EXPECT(acr_mtl_parse(mtl, &m));
    EXPECT(m.count >= 1 && m.count <= ACR_MTL_MAX);
    EXPECT(acr_obj_parse(vis, &m, &mesh) && mesh.tri_count > 0);
    /* The converter's textures (one per colour, 64 x 128: walls above
     * floors): each loads with the game's PNG reader, and since stage
     * textures don't repeat, every face stays inside its picture. */
    int textured = 0;
    for (int i = 0; i < m.count; ++i) {
        if (!m.m[i].textured) continue;
        ++textured;
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", dir, m.m[i].texture);
        FILE *f = fopen(path, "rb");
        EXPECT(f != NULL);
        if (!f) continue;
        static unsigned char png[65536];
        const size_t n = fread(png, 1, sizeof png, f);
        fclose(f);
        acr_image img;
        const int ok = acr_png_decode(png, n, &img);
        EXPECT(ok);
        if (ok) {
            EXPECT(img.w == 64 && img.h == 128);
            acr_image_free(&img);
        }
    }
    if (textured) {
        EXPECT(textured == m.count);
        int outside = 0, walls = 0, floors = 0;
        for (int t = 0; t < mesh.tri_count * 3; ++t) {
            const float *uv = mesh.v[t].uv;
            if (uv[0] < 0.0f || uv[0] > 1.0f || uv[1] < 0.0f || uv[1] > 1.0f) ++outside;
            /* never on the line between the two halves */
            if (uv[1] > 0.5f) ++walls; else if (uv[1] < 0.5f) ++floors; else ++outside;
        }
        EXPECT(outside == 0 && walls > 0 && floors > 0);
    }
    if (sample) {
        EXPECT(textured > 0);
        /* The converter's own picture of the map (preview.png): there, and a PNG. */
        char path[1400];
        snprintf(path, sizeof path, "%s/preview.png", dir);
        FILE *f = fopen(path, "rb");
        EXPECT(f != NULL);
        if (f) {
            static unsigned char png[2 * 1024 * 1024];
            const size_t n = fread(png, 1, sizeof png, f);
            fclose(f);
            acr_image img;
            const int ok = acr_png_decode(png, n, &img);
            EXPECT(ok);
            if (ok) {
                EXPECT(img.w == 640 && img.h == 400);
                acr_image_free(&img);
            }
        }
    }
    acr_mesh_free(&mesh);

    acr_gen1_stage s;
    EXPECT(acr_gen1_stage_parse(coll, &s));
    int32_t st[2][4];
    EXPECT(start(toml, "start_1", st[0]) && start(toml, "start_2", st[1]));

    /* Each start stands on a floor. */
    for (int k = 0; k < 2; ++k) {
        int32_t fy = 0;
        EXPECT(acr_gen1_stage_floor(&s, st[k][0], st[k][2], st[k][1] - 50, &fy));
        EXPECT(fy == st[k][1]);
    }
    /* The two face each other. */
    EXPECT(((st[0][3] + 2048) & 4095) == st[1][3]);

    const int tiles = sample && strstr(toml, "name = \"Sample Tiles\"") != NULL;
    if (tiles) {
        /* 200 mm tiles = 10,000 units; the three middles end up at A1 (-5,000,
         * -5,000), B1 (5,000, -5,000), A2 (-5,000, 5,000). */
        EXPECT(st[0][0] == -5000 && st[0][2] == -5000 && st[0][3] == 1024);
        EXPECT(st[1][0] == 5000 && st[1][2] == -5000 && st[1][3] == 3072);
        /* The joins: floor on both sides, and nothing to push against. */
        int32_t fy = 0;
        EXPECT(acr_gen1_stage_floor(&s, -20, -5000, -300, &fy) && fy == -200);
        EXPECT(acr_gen1_stage_floor(&s, 20, -5000, -300, &fy) && fy == -200);
        EXPECT(acr_gen1_stage_floor(&s, -5000, 20, -300, &fy) && fy == -200);
        int32_t x = 0, z = -5000;
        EXPECT(!acr_gen1_stage_push(&s, &x, &z, -200, 212, 100, 120));
        x = -5000; z = 0;
        EXPECT(!acr_gen1_stage_push(&s, &x, &z, -200, 212, 100, 120));
        /* A2's pillar, 50 mm north of its middle before the quarter turn, now
         * 2,500 east of it: faces at x -2,900 / -2,100. */
        x = -2050; z = 5000;
        EXPECT(acr_gen1_stage_push(&s, &x, &z, -200, 212, 100, 120));
        EXPECT(x >= -2000);
        /* A shot from start 1 to start 2 crosses the join without hitting anything. */
        const int32_t a[3] = { -5000, -500, -5000 }, b[3] = { 5000, -500, -5000 };
        int32_t t = 0;
        EXPECT(!acr_gen1_stage_segment(&s, a, b, &t));
    } else if (sample) {
        /* make_sample.py: floor top 4 mm = y -200; walls' inner faces at 126 mm
         * = 6,300; pillars 42-58 mm = 2,100-2,900 out; the ledge's top 20 mm. */
        EXPECT(st[0][1] == -200 && st[0][2] == -4500 && st[0][3] == 0);
        EXPECT(st[1][2] == 4500 && st[1][3] == 2048);
        /* Two green and two red markers, 10 mm (500) either side: teammate pairs. */
        const char *t1 = strstr(toml, "team_1 = "), *t2 = strstr(toml, "team_2 = ");
        int32_t m[4][4];
        EXPECT(t1 && sscanf(t1, "team_1 = [[%d, %d, %d, %d], [%d, %d, %d, %d]]", &m[0][0], &m[0][1], &m[0][2], &m[0][3],
                            &m[1][0], &m[1][1], &m[1][2], &m[1][3]) == 8);
        EXPECT(t2 && sscanf(t2, "team_2 = [[%d, %d, %d, %d], [%d, %d, %d, %d]]", &m[2][0], &m[2][1], &m[2][2], &m[2][3],
                            &m[3][0], &m[3][1], &m[3][2], &m[3][3]) == 8);
        EXPECT(m[0][0] == -500 && m[1][0] == 500 && m[0][2] == -4500 && m[0][1] == -200);
        EXPECT(m[2][0] == -500 && m[3][0] == 500 && m[2][2] == 4500 && m[3][3] == 2048);
        int32_t x = 6250, z = 0;   /* half inside the west/east wall */
        EXPECT(acr_gen1_stage_push(&s, &x, &z, -200, 212, 100, 120));
        EXPECT(x <= 6200);
        x = -6250; z = -3000;
        EXPECT(acr_gen1_stage_push(&s, &x, &z, -200, 212, 100, 120));
        EXPECT(x >= -6200);
        /* A pillar: an AC put against its south face is pushed out. */
        x = 2500; z = 2050;
        EXPECT(acr_gen1_stage_push(&s, &x, &z, -200, 212, 100, 120));
        EXPECT(z <= 2000);
        /* The ledge (east, top 20 mm = -1,000) is a floor of its own. */
        int32_t fy = 0;
        EXPECT(acr_gen1_stage_floor(&s, 5000, 0, -1100, &fy) && fy == -1000);
        /* A shot down the middle between the starts flies clear; one east hits the wall. */
        const int32_t a[3] = { 0, -500, -4500 }, b[3] = { 0, -500, 4500 }, c[3] = { 9000, -500, -4500 };
        int32_t t = 0;
        EXPECT(!acr_gen1_stage_segment(&s, a, b, &t));
        EXPECT(acr_gen1_stage_segment(&s, a, c, &t) && t > 0 && t < 1024);
    }
    acr_gen1_stage_free(&s);
    free(toml); free(coll); free(vis); free(mtl);
    printf(failures ? "custom stage: %d failures\n" : "custom stage: ok\n", failures);
    return failures != 0;
}
