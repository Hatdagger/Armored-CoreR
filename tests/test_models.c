/* Model exporter pieces that need no disc: slot order, CRC, PNG writer. */
#include <stdio.h>
#include <string.h>

#include "acr_models_moa.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static int is(const acr_moa_role *r, unsigned part, unsigned piece) {
    return r->part == part && r->piece == piece;
}

int main(int argc, char **argv) {
    acr_moa_role r[40];
    /* Standard arms and legs: 23 slots, hand after the arms, back units last. */
    CHECK(acr_moa_slot_roles(7, 7, r, 40) == 23);
    CHECK(is(&r[0], ACR_MOA_HEAD, 0) && is(&r[1], ACR_MOA_CORE, 0) && is(&r[2], ACR_MOA_LEGS, 0));
    CHECK(is(&r[3], ACR_MOA_ARMS, 0) && is(&r[9], ACR_MOA_ARMS, 6) && is(&r[10], ACR_MOA_HAND, 0));
    CHECK(is(&r[11], ACR_MOA_LEGS, 1) && is(&r[16], ACR_MOA_LEGS, 6));
    CHECK(is(&r[17], ACR_MOA_BACK_RIGHT, 0) && is(&r[22], ACR_MOA_BACK_RIGHT, 1));
    /* Weapon arms (8 pieces) have no hand. */
    CHECK(acr_moa_slot_roles(8, 7, r, 40) == 23);
    CHECK(is(&r[10], ACR_MOA_ARMS, 7) && is(&r[11], ACR_MOA_LEGS, 1));
    /* Caterpillars: one legs piece. */
    CHECK(acr_moa_slot_roles(7, 1, r, 40) == 17);
    CHECK(is(&r[11], ACR_MOA_BACK_RIGHT, 0));
    /* Four legs: 4 pieces before the back units, 4 after. */
    CHECK(acr_moa_slot_roles(7, 9, r, 40) == 25);
    CHECK(is(&r[14], ACR_MOA_LEGS, 4) && is(&r[15], ACR_MOA_BACK_RIGHT, 0));
    CHECK(is(&r[20], ACR_MOA_BACK_RIGHT, 1) && is(&r[21], ACR_MOA_LEGS, 5) && is(&r[24], ACR_MOA_LEGS, 8));
    /* Too small a buffer still reports the full count. */
    CHECK(acr_moa_slot_roles(7, 7, r, 4) == 23);

    /* CRC-32 check value and the IEND chunk's well-known CRC. */
    CHECK(acr_crc32((const uint8_t *)"123456789", 9, 0) == 0xCBF43926u);
    CHECK(acr_crc32((const uint8_t *)"IEND", 4, 0) == 0xAE426082u);

    /* A 3x2 indexed PNG: signature, size and the IHDR fields. */
    char path[512];
    snprintf(path, sizeof path, "%s", argc > 1 ? argv[1] : "test_models.png");
    uint8_t pix[6] = { 0, 1, 2, 3, 4, 5 }, pal[768], alpha[256];
    for (int i = 0; i < 768; ++i) pal[i] = (uint8_t)i;
    memset(alpha, 255, sizeof alpha);
    alpha[0] = 0;
    CHECK(acr_write_png8(path, 3, 2, pix, pal, alpha));
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        uint8_t head[33];
        CHECK(fread(head, 1, sizeof head, f) == sizeof head);
        fclose(f);
        CHECK(memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0);
        CHECK(memcmp(head + 12, "IHDR", 4) == 0);
        CHECK(head[19] == 3 && head[23] == 2 && head[24] == 8 && head[25] == 3);
    }
    CHECK(!acr_write_png8(path, 0, 2, pix, pal, NULL));

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("test_models: ok\n");
    return 0;
}
