/* Briefing codes: every address survives the trip through a briefing and
 * through its pasted text, typos and the wrong kind of code are caught, and
 * the word lists keep their rules (sizes, alphabetical, no word twice). */
#include <stdio.h>
#include <string.h>

#include "acr_brief.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static uint32_t seed = 777;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8 ^ seed << 13; }

static int ci_cmp(const char *a, const char *b) {
    for (;; ++a, ++b) {
        int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y || !x) return x - y;
    }
}

int main(void) {
    /* Word lists. */
    const char *all[1024];
    int all_n = 0;
    for (int f = 0; f < ACR_BRIEF_FIELDS; ++f) {
        if (f == ACR_BRIEF_PAYOUT || f == ACR_BRIEF_TIME) continue;
        char tmp[16];
        for (int i = 0; i < acr_brief_choices(f); ++i) {
            const char *w = acr_brief_choice(f, i, tmp, sizeof tmp);
            EXPECT(w[0] && !strchr(w, ' '));
            if (i) EXPECT(ci_cmp(acr_brief_choice(f, i - 1, tmp, sizeof tmp), w) < 0);
            for (int k = 0; k < all_n; ++k) if (!ci_cmp(all[k], w)) { printf("twice: %s\n", w); ++failures; }
            all[all_n++] = w;
        }
    }
    EXPECT(all_n == 64 + 64 + 128 * 4 + 256);

    /* Round trips. */
    int typo_passed = 0, typos = 0;
    for (int t = 0; t < 20000; ++t) {
        const uint32_t a = rnd() ^ rnd() << 16;
        uint8_t ip[4] = { (uint8_t)(a >> 24), (uint8_t)(a >> 16), (uint8_t)(a >> 8), (uint8_t)a }, ip2[4];
        const uint16_t port = (uint16_t)(rnd() % 65535 + 1);
        uint16_t port2 = 0;
        const int kind = t & 1;
        acr_brief b, p;
        acr_brief_encode(ip, port, kind, &b);
        for (int f = 0; f < ACR_BRIEF_FIELDS; ++f) EXPECT(b.v[f] < acr_brief_choices(f));
        EXPECT(acr_brief_decode(&b, kind, ip2, &port2) && !memcmp(ip, ip2, 4) && port == port2);
        EXPECT(!acr_brief_decode(&b, !kind, ip2, &port2));
        char text[300];
        acr_brief_format(&b, kind, text, sizeof text);
        EXPECT(acr_brief_parse(text, &p) && !memcmp(&b, &p, sizeof b));
        if (t == 0) printf("%s\n", text);
        /* One field picked wrong. */
        const int f = (int)(rnd() % ACR_BRIEF_FIELDS);
        p = b;
        p.v[f] = (uint16_t)((p.v[f] + 1 + rnd() % (uint32_t)(acr_brief_choices(f) - 1)) % (uint32_t)acr_brief_choices(f));
        ++typos;
        if (acr_brief_decode(&p, kind, ip2, &port2)) ++typo_passed;
    }
    printf("typos that passed the check: %d of %d\n", typo_passed, typos);
    EXPECT(typo_passed < typos / 2000 + 3);

    /* Typed by hand, odd spacing and case, payout without thousands, time without colon. */
    acr_brief b, p;
    const uint8_t ip[4] = { 203, 0, 113, 7 };
    acr_brief_encode(ip, 7777, ACR_BRIEF_OFFER, &b);
    char s[200], line[300], pay[16];
    acr_brief_sentence(&b, s, sizeof s);
    snprintf(line, sizeof line, "  %s   payout: %s   time %02d%02d  wingman %s ", s,
             acr_brief_choice(ACR_BRIEF_PAYOUT, b.v[ACR_BRIEF_PAYOUT], pay, sizeof pay) /* "48,000 C" */,
             b.v[ACR_BRIEF_TIME] / 60, b.v[ACR_BRIEF_TIME] % 60, acr_brief_wingman[b.v[ACR_BRIEF_WINGMAN]]);
    for (char *c = line; *c; ++c) if (*c >= 'a' && *c <= 'z' && (c - line) % 3 == 0) *c = (char)(*c - 32);
    EXPECT(acr_brief_parse(line, &p) && !memcmp(&b, &p, sizeof b));
    EXPECT(!acr_brief_parse("Destroy the base.", &p));

    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("brief: ok\n");
    return 0;
}
