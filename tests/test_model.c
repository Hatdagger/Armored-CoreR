/* Model loading with made-up files: PNG reader, skeleton, animations, pose
 * matrices, OBJ/MTL and the colour ramp. No game data. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acr_model.h"
#include "acr_models_moa.h"
#include "acr_png.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 0.01f)

static const char k_skel[] =
    "# made up\n"
    "[[node]]\nid       = 7\nparent   = 3\nslot     = 1\ndraws    = \"arms 2\"\nposition = [-10, 20, 0]\n\n"
    "[[node]]\nid       = 3\nparent   = 5\nposition = [10, -20, 0]\nrotation = [0, 1024, 0]\n\n"
    "[[node]]\nid       = 5\nparent   = -1\nslot     = 0\ndraws    = \"back-left 0\"\nposition = [0, 0, 0]\n";

static const char k_anims[] =
    "anim 0 idle\nlast_tick 0\nframes 1\nhold\nframe 0\n  node 3 rot 0 1024 0 pos 10 -20 0\n\n"
    "anim 5\nlast_tick 5\nframes 3\nhold 2 3\nframe 0\n  node 3 rot zero pos 10 -20 0\n"
    "frame 1\n  node 3 rot 0 0 1024\nframe 2\n  node 3 rot zero pos 5 0 0\n  node 99 rot 1 2 3\n";

static const char k_mtl[] =
    "# made up\nnewmtl arms\nKd 1 1 1\nmap_Kd texture.png\n\nnewmtl rgb-FF0000-semi\nKd 1.0000 0.0000 0.0000\nd 0.5\n"
    "\nnewmtl back-left-2side\nKd 1 1 1\nmap_Kd ../../back-left/00/texture.png\n";

static const char k_obj[] =
    "mtllib part.mtl\no piece0\nv 1 2 3\nv 4 5 6\nv 7 8 9\nv 0 0 1\nvt 0.25 0.75\nvt 0.5 0.5\nvt 1 0\nvt 0 1\n"
    "vn 0 1 0\nusemtl rgb-FF0000-semi\nf 1//1 2//1 3//1\nusemtl arms\nf 1/1/1 2/2/1 3/3/1 4/4/1\n";

/* Two zlib streams made with Python's zlib from made-up bytes: fixed and
 * dynamic Huffman blocks. */
static const unsigned char k_fixed[] = { 0x78, 0xda, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40,
                                         0x90, 0x00, 0x3a, 0x2e, 0x06, 0x7d };
static const unsigned char k_dynamic[] = {
    0x78, 0xda, 0xed, 0xcd, 0xb9, 0x11, 0xc0, 0x30, 0x0c, 0xc0, 0xb0, 0x3d, 0xd9, 0xc9, 0xfa, 0xbc, 0xff, 0x04,
    0xe6, 0x1c, 0xb9, 0xb4, 0x68, 0xc0, 0xe9, 0x3c, 0x6c, 0x65, 0xd3, 0x3d, 0xc1, 0x6c, 0x5e, 0xa2, 0x22, 0xd0,
    0x13, 0xfd, 0xa2, 0x2f, 0xfa, 0xa2, 0x1f, 0xf4, 0x45, 0x6f, 0xf4, 0x41, 0x0f, 0xf4, 0x46, 0x2f, 0xf4, 0x46,
    0x5f, 0xf4, 0x42, 0x4f, 0xf4, 0x44, 0x1f, 0xf4, 0x44, 0x0f, 0xfe, 0xf7, 0xd3, 0xef, 0x03, 0x54, 0x1a, 0xe6,
    0x51 };

static void apply(const float m[12], const float p[3], float o[3]) {
    for (int r = 0; r < 3; ++r) o[r] = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3];
}

int main(int argc, char **argv) {
    /* ---- inflate ---- */
    uint8_t out[1024];
    CHECK(acr_inflate_zlib(k_fixed, sizeof k_fixed, out, sizeof out) == 17);
    CHECK(memcmp(out, "hello hello hello", 17) == 0);
    CHECK(acr_inflate_zlib(k_dynamic, sizeof k_dynamic, out, sizeof out) == 600);
    CHECK(acr_crc32(out, 600, 0) == 2797204642u);
    CHECK(acr_inflate_zlib(k_dynamic, sizeof k_dynamic, out, 100) == -1);   /* too small */
    CHECK(acr_inflate_zlib(k_fixed, 1, out, sizeof out) == -1);

    /* ---- PNG round trip through the exporter's writer (stored blocks) ---- */
    const char *png_path = argc > 1 ? argv[1] : "test_model.png";
    uint8_t pix[12], pal[768], alpha[256];
    for (int i = 0; i < 12; ++i) pix[i] = (uint8_t)(i * 20);
    for (int i = 0; i < 768; ++i) pal[i] = (uint8_t)(255 - i % 256);
    memset(alpha, 255, sizeof alpha);
    alpha[0] = 0;
    CHECK(acr_write_png8(png_path, 4, 3, pix, pal, alpha));
    FILE *f = fopen(png_path, "rb");
    uint8_t file[4096];
    const size_t n = f ? fread(file, 1, sizeof file, f) : 0;
    if (f) fclose(f);
    acr_image img;
    CHECK(acr_png_decode(file, n, &img));
    CHECK(img.w == 4 && img.h == 3 && img.indexed);
    CHECK(img.pixels && img.pixels[5] == 100 && img.pixels[11] == 220);
    CHECK(img.palette[1][0] == 252 && img.palette[0][3] == 0 && img.palette[1][3] == 255);
    acr_image_free(&img);
    CHECK(!acr_png_decode(file, 20, &img));

    /* ---- skeleton: parents first, ids mapped, draws parsed ---- */
    acr_skeleton sk;
    CHECK(acr_skeleton_parse(k_skel, &sk));
    CHECK(sk.count == 3);
    CHECK(sk.node[0].id == 5 && sk.node[0].parent == -1 && sk.node[0].draw_kind == ACR_PART_BACK_LEFT);
    CHECK(sk.node[1].id == 3 && sk.node[1].parent == 0 && sk.node[1].draw_kind == ACR_PART_NONE);
    CHECK(sk.node[2].id == 7 && sk.node[2].parent == 1 && sk.node[2].draw_kind == ACR_PART_ARMS && sk.node[2].draw_piece == 2);
    CHECK(sk.index_of[7] == 2 && sk.index_of[4] == -1);
    CHECK(sk.node[1].rot[1] == 1024 && sk.node[2].pos[1] == 20);
    acr_skeleton bad;
    CHECK(!acr_skeleton_parse("[[node]]\nid = 1\nparent = 9\n", &bad));   /* missing parent */

    /* ---- animations and hold times ---- */
    acr_anim_set as;
    CHECK(acr_anim_set_parse(k_anims, &sk, &as));
    CHECK(as.anim[0].frames == 1 && as.anim[5].frames == 3 && as.anim[5].ticks == 5 && as.anim[1].frames == 0);
    CHECK(as.frame[as.anim[5].first_frame + 2].keys == 1);   /* node 99 is not in the skeleton */
    CHECK(acr_anim_frame_at(&as, 5, 0) == 0 && acr_anim_frame_at(&as, 5, 1) == 0);
    CHECK(acr_anim_frame_at(&as, 5, 2) == 1 && acr_anim_frame_at(&as, 5, 4) == 1);
    CHECK(acr_anim_frame_at(&as, 5, 5) == 2 && acr_anim_frame_at(&as, 5, 100) == 2);
    CHECK(acr_anim_frame_at(&as, 9, 0) == -1);
    acr_pose pose;
    acr_pose_play(&sk, &as, 5, 3, &pose);
    CHECK(pose.rot[1][0] == 0 && pose.rot[1][1] == 0 && pose.rot[1][2] == 1024 && pose.pos[1][0] == 10);
    /* later frames are steps; "zero" in a step frame changes nothing */
    acr_pose_play(&sk, &as, 5, 9, &pose);
    CHECK(pose.pos[1][0] == 15 && pose.pos[1][1] == -20 && pose.rot[1][2] == 1024);
    acr_pose_play(&sk, &as, 30, 0, &pose);                    /* missing: bind pose */
    CHECK(pose.rot[1][1] == 1024);

    /* ---- matrices: a joint turned a quarter about game y, a pivot below it ---- */
    float m[ACR_SKEL_MAX_NODES][12], o[3];
    acr_pose_bind(&sk, &pose);
    acr_pose_matrices(&sk, &pose, m);
    /* Pivot 7 (position -10, 20, 0) under joint 3 (10, -20, 0; Ry 90 deg):
     * its piece matrix is joint 3's: model space = F (T(10,-20,0) Ry) F. The
     * joint's own place (model (10, 20, 0)) stays put; game +x turns to game
     * -z under Ry(90), i.e. model +z. */
    const float origin[3] = { 0, 0, 0 }, unit_x[3] = { 1, 0, 0 };
    apply(m[2], origin, o);
    CHECK(NEAR(o[0], 10) && NEAR(o[1], 20) && NEAR(o[2], 0));
    apply(m[2], unit_x, o);
    CHECK(NEAR(o[0], 10) && NEAR(o[1], 20) && NEAR(o[2], 1));
    /* The root pivot at the origin is the identity. */
    apply(m[0], unit_x, o);
    CHECK(NEAR(o[0], 1) && NEAR(o[1], 0) && NEAR(o[2], 0));
    /* Rotation order Rx * Ry: game -z -> Ry(90) -> game -x; Rx leaves x alone. */
    acr_skeleton one;
    CHECK(acr_skeleton_parse("[[node]]\nid = 0\nparent = -1\nrotation = [1024, 1024, 0]\n", &one));
    acr_pose_bind(&one, &pose);
    acr_pose_matrices(&one, &pose, m);
    const float unit_z[3] = { 0, 0, 1 };
    apply(m[0], unit_z, o);           /* model +z = game -z -> game -x -> model -x */
    CHECK(NEAR(o[0], -1) && NEAR(o[1], 0) && NEAR(o[2], 0));
    acr_anim_set_free(&as);

    /* ---- MTL / OBJ ---- */
    acr_mtl mtl;
    CHECK(acr_mtl_parse(k_mtl, &mtl));
    CHECK(mtl.count == 3);
    CHECK(mtl.m[0].textured && mtl.m[0].kind == ACR_PART_ARMS && !mtl.m[0].semi);
    CHECK(!mtl.m[1].textured && mtl.m[1].semi && mtl.m[1].r == 255 && mtl.m[1].g == 0 && mtl.m[1].kind == ACR_PART_NONE);
    CHECK(mtl.m[2].two_side && mtl.m[2].kind == ACR_PART_BACK_LEFT && !strcmp(mtl.m[2].texture, "../../back-left/00/texture.png"));
    acr_mesh mesh;
    CHECK(acr_obj_parse(k_obj, &mtl, &mesh));
    CHECK(mesh.tri_count == 3);                              /* the quad is fanned */
    CHECK(mesh.material[0] == 1 && mesh.material[1] == 0 && mesh.material[2] == 0);
    CHECK(mesh.v[0].p[2] == 3 && mesh.v[3].uv[0] == 0.25f && mesh.v[8].p[0] == 0 && mesh.v[8].n[1] == 1);
    acr_mesh_free(&mesh);
    CHECK(!acr_obj_parse("v 0 0 0\nf 1 2 3\n", &mtl, &mesh));   /* bad index */

    /* ---- colour ramp ---- */
    const uint8_t cols[4][3] = { { 63, 0, 31 }, { 16, 16, 16 }, { 0, 0, 0 }, { 31, 31, 31 } };
    uint8_t ramp[256][4];
    acr_colour_ramp(cols, ramp);
    CHECK(ramp[0][0] == 255 && ramp[0][1] == 0 && ramp[0][2] == 255);    /* full at the top */
    CHECK(ramp[63][0] == 255);                                           /* 61+ stays full */
    CHECK(ramp[64][0] == (16 << 3 | 16 >> 2));
    CHECK(ramp[64 + 2][0] == ramp[64 + 3][0]);                           /* even shades */
    CHECK(ramp[127][0] == (1 << 3 | 1 >> 2));                            /* down towards 1 */
    CHECK(ramp[128][0] == 0 && ramp[255][3] == 255);
    {   /* the default BASE green (36) runs 31 -> 6 in the game */
        const uint8_t def[4][3] = { { 32, 36, 28 }, { 0 }, { 0 }, { 0 } };
        acr_colour_ramp(def, ramp);
        CHECK(ramp[0][1] == 255 && ramp[63][1] >= (6 << 3) && ramp[63][1] < (8 << 3));
    }

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("test_model: ok\n");
    return 0;
}
