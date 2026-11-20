#include "acr_brief.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int k_choices[ACR_BRIEF_FIELDS] = { 64, 64, 128, 128, 128, 128, 99, 1440, 256 };
static const char *const k_label[ACR_BRIEF_FIELDS] = {
    "ACTION", "PLACE", "PLACE", "AREA", "STRUCTURE", "TARGET", "PAYOUT", "TIME", "WINGMAN"
};

static const char *const *list(int field) {
    switch (field) {
    case ACR_BRIEF_ACTION: return acr_brief_action;
    case ACR_BRIEF_PLACE_A: return acr_brief_place_a;
    case ACR_BRIEF_PLACE_B: return acr_brief_place_b;
    case ACR_BRIEF_POSITION: return acr_brief_position;
    case ACR_BRIEF_STRUCTURE: return acr_brief_structure;
    case ACR_BRIEF_TARGET: return acr_brief_target;
    case ACR_BRIEF_WINGMAN: return acr_brief_wingman;
    default: return NULL;
    }
}

int acr_brief_choices(int field) { return field >= 0 && field < ACR_BRIEF_FIELDS ? k_choices[field] : 0; }
const char *acr_brief_label(int field) { return field >= 0 && field < ACR_BRIEF_FIELDS ? k_label[field] : ""; }

const char *acr_brief_choice(int field, int choice, char *out, int size) {
    if (choice < 0 || choice >= acr_brief_choices(field)) return "?";
    if (field == ACR_BRIEF_PAYOUT) {
        snprintf(out, (size_t)size, "%d,000 C", choice + 1);
        return out;
    }
    if (field == ACR_BRIEF_TIME) {
        snprintf(out, (size_t)size, "%02d:%02d", choice / 60, choice % 60);
        return out;
    }
    return list(field)[choice];
}

/* ---------------- bits ---------------- */

static uint64_t inverse(uint64_t k) {     /* k odd: k * inverse(k) == 1 (mod 2^64) */
    uint64_t x = k;
    for (int i = 0; i < 6; ++i) x *= 2 - k * x;
    return x;
}
static uint64_t unxorshift(uint64_t y, int s) {
    uint64_t x = y;
    for (int i = 0; i < 64 / s + 1; ++i) x = y ^ (x >> s);
    return x;
}
#define K1 0xBF58476D1CE4E5B9ull
#define K2 0x94D049BB133111EBull
static uint64_t mix(uint64_t x) {
    x ^= x >> 31; x *= K1; x ^= x >> 29; x *= K2; x ^= x >> 32;
    return x;
}
static uint64_t unmix(uint64_t x) {
    x = unxorshift(x, 32); x *= inverse(K2); x = unxorshift(x, 29); x *= inverse(K1); x = unxorshift(x, 31);
    return x;
}

static uint16_t check(const uint8_t ip[4], uint16_t port, int kind) {
    const uint8_t b[] = { 'A', 'C', 'R', 'B', ip[0], ip[1], ip[2], ip[3],
                          (uint8_t)(port >> 8), (uint8_t)port, (uint8_t)kind };
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof b; ++i) { h ^= b[i]; h *= 16777619u; }
    return (uint16_t)(h ^ h >> 16);
}

void acr_brief_encode(const uint8_t ip[4], uint16_t port, int kind, acr_brief *out) {
    uint64_t v = (uint64_t)ip[0] << 56 | (uint64_t)ip[1] << 48 | (uint64_t)ip[2] << 40 | (uint64_t)ip[3] << 32 |
                 (uint64_t)port << 16 | check(ip, port, kind);
    v = mix(v);
    for (int f = 0; f < ACR_BRIEF_FIELDS; ++f) {
        out->v[f] = (uint16_t)(v % (uint64_t)k_choices[f]);
        v /= (uint64_t)k_choices[f];
    }
}

int acr_brief_decode(const acr_brief *b, int kind, uint8_t ip[4], uint16_t *port) {
    uint64_t v = 0;
    for (int f = ACR_BRIEF_FIELDS - 1; f >= 0; --f) {
        const uint64_t r = (uint64_t)k_choices[f];
        if (b->v[f] >= r) return 0;
        if (v > (UINT64_MAX - b->v[f]) / r) return 0;     /* past 64 bits: not a code */
        v = v * r + b->v[f];
    }
    v = unmix(v);
    for (int i = 0; i < 4; ++i) ip[i] = (uint8_t)(v >> (56 - 8 * i));
    *port = (uint16_t)(v >> 16);
    return (uint16_t)v == check(ip, *port, kind) && *port != 0;
}

/* ---------------- text ---------------- */

int acr_brief_sentence(const acr_brief *b, char *out, int size) {
    char act[32];
    snprintf(act, sizeof act, "%s", acr_brief_action[b->v[ACR_BRIEF_ACTION] % 64]);
    if (act[0] >= 'a' && act[0] <= 'z') act[0] = (char)(act[0] - 'a' + 'A');
    return snprintf(out, (size_t)size, "%s %s %s %s %s %s.", act,
                    acr_brief_place_a[b->v[ACR_BRIEF_PLACE_A] % 64], acr_brief_place_b[b->v[ACR_BRIEF_PLACE_B] % 128],
                    acr_brief_position[b->v[ACR_BRIEF_POSITION] % 128], acr_brief_structure[b->v[ACR_BRIEF_STRUCTURE] % 128],
                    acr_brief_target[b->v[ACR_BRIEF_TARGET] % 128]);
}

int acr_brief_format(const acr_brief *b, int kind, char *out, int size) {
    char s[160], pay[16], tm[8];
    acr_brief_sentence(b, s, sizeof s);
    return snprintf(out, (size_t)size, "%s: %s Payout %s. Time %s. Wingman %s.",
                    kind == ACR_BRIEF_REPLY ? "READ-BACK" : "MISSION BRIEFING", s,
                    acr_brief_choice(ACR_BRIEF_PAYOUT, b->v[ACR_BRIEF_PAYOUT], pay, sizeof pay),
                    acr_brief_choice(ACR_BRIEF_TIME, b->v[ACR_BRIEF_TIME], tm, sizeof tm),
                    acr_brief_wingman[b->v[ACR_BRIEF_WINGMAN] % 256]);
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }
static int same_word(const char *a, const char *tok) {
    while (*a && *tok && lower((unsigned char)*a) == *tok) ++a, ++tok;
    return !*a && !*tok;
}
static int find(int field, const char *tok) {
    const char *const *l = list(field);
    for (int i = 0; i < k_choices[field]; ++i)
        if (same_word(l[i], tok)) return i;
    return -1;
}
static int is_number(const char *t) {
    if (!*t) return 0;
    for (; *t; ++t) if (*t < '0' || *t > '9') return 0;
    return 1;
}

int acr_brief_parse(const char *text, acr_brief *out) {
    /* Words of letters and digits; commas vanish (48,000 -> 48000). */
    char buf[512];
    int n = 0;
    for (const char *p = text; *p && n < (int)sizeof buf - 1; ++p) {
        const int c = lower((unsigned char)*p);
        if (c == ',') continue;
        buf[n++] = (char)((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c : ' ');
    }
    buf[n] = 0;
    int field = 0, nums[3], num_n = 0;
    memset(out, 0, sizeof *out);
    for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) {
        if (field < ACR_BRIEF_PAYOUT) {
            const int i = find(field, t);
            if (i >= 0) out->v[field++] = (uint16_t)i;
        } else if (field < ACR_BRIEF_WINGMAN) {
            if (is_number(t) && num_n < 3) {
                long v = strtol(t, NULL, 10);
                if (num_n == 1 && strlen(t) == 4) {          /* "0340" */
                    nums[num_n++] = (int)(v / 100);
                    v %= 100;
                }
                nums[num_n++] = (int)v;
                if (num_n == 3) field = ACR_BRIEF_WINGMAN;
            }
        } else {
            const int i = find(ACR_BRIEF_WINGMAN, t);
            if (i >= 0) { out->v[ACR_BRIEF_WINGMAN] = (uint16_t)i; ++field; break; }
        }
    }
    if (field <= ACR_BRIEF_WINGMAN) return 0;
    int pay = nums[0] >= 1000 ? nums[0] / 1000 : nums[0];
    if (pay < 1 || pay > 99 || nums[1] > 23 || nums[2] > 59) return 0;
    out->v[ACR_BRIEF_PAYOUT] = (uint16_t)(pay - 1);
    out->v[ACR_BRIEF_TIME] = (uint16_t)(nums[1] * 60 + nums[2]);
    return 1;
}
