/* backend/clipseal.h: the sealed clipboard hand-off (lane clipseal). GPLv3. */
#include "check.h"
#include "../../backend/clipseal.h"

#include <time.h>

static void test_round_trip(void)
{
    uint8_t key[MLXR_CLIPSEAL_KEY_BYTES];
    CHECK(mlxr_clipseal_new_key(key) == 0);
    const char *texts[] = { "", "x", "password: hunter2", "naïve café 日本語 🙂\nsecond line\t\x01" };
    for (size_t k = 0; k < sizeof texts / sizeof texts[0]; k++) {
        size_t n = strlen(texts[k]), sl = 0, tl = 99;
        uint8_t sealed[256];
        char back[256];
        CHECK(mlxr_clipseal_seal(key, MLXR_CLIPSEAL_TO_HOST, texts[k], n, sealed, sizeof sealed, &sl) == 0);
        CHECK_EQ_INT(sl, n + MLXR_CLIPSEAL_OVERHEAD);
        CHECK(memcmp(sealed, MLXR_CLIPSEAL_MAGIC, MLXR_CLIPSEAL_MAGIC_BYTES) == 0);
        /* the text itself is nowhere in the sealed bytes */
        if (n >= 4) CHECK(memmem(sealed, sl, texts[k], n) == NULL);
        CHECK(mlxr_clipseal_open(key, MLXR_CLIPSEAL_TO_HOST, sealed, sl, back, sizeof back, &tl) == 0);
        CHECK_EQ_INT(tl, n);
        CHECK_EQ_STR(back, texts[k]);
    }
}

static void test_refusals(void)
{
    uint8_t key[MLXR_CLIPSEAL_KEY_BYTES], other[MLXR_CLIPSEAL_KEY_BYTES];
    CHECK(mlxr_clipseal_new_key(key) == 0 && mlxr_clipseal_new_key(other) == 0);
    CHECK(memcmp(key, other, sizeof key) != 0);
    const char *t = "secret text from the PC";
    size_t n = strlen(t), sl = 0, tl = 0;
    uint8_t sealed[128], bad[128];
    char back[128];
    CHECK(mlxr_clipseal_seal(key, MLXR_CLIPSEAL_FROM_HOST, t, n, sealed, sizeof sealed, &sl) == 0);
    /* another key, the other direction */
    memset(back, 'Z', sizeof back);
    CHECK(mlxr_clipseal_open(other, MLXR_CLIPSEAL_FROM_HOST, sealed, sl, back, sizeof back, &tl) == -1);
    CHECK(memmem(back, sizeof back, "secret", 6) == NULL);
    CHECK(mlxr_clipseal_open(key, MLXR_CLIPSEAL_TO_HOST, sealed, sl, back, sizeof back, &tl) == -1);
    /* any byte changed: magic, nonce, body or tag */
    for (size_t i = 0; i < sl; i++) {
        memcpy(bad, sealed, sl);
        bad[i] ^= 0x01;
        CHECK(mlxr_clipseal_open(key, MLXR_CLIPSEAL_FROM_HOST, bad, sl, back, sizeof back, &tl) == -1);
    }
    /* cut short, too short, no room */
    CHECK(mlxr_clipseal_open(key, MLXR_CLIPSEAL_FROM_HOST, sealed, sl - 1, back, sizeof back, &tl) == -1);
    CHECK(mlxr_clipseal_open(key, MLXR_CLIPSEAL_FROM_HOST, sealed, MLXR_CLIPSEAL_OVERHEAD - 1, back, sizeof back, &tl) == -1);
    CHECK(mlxr_clipseal_open(key, MLXR_CLIPSEAL_FROM_HOST, sealed, sl, back, n, &tl) == -1);
    CHECK(mlxr_clipseal_seal(key, MLXR_CLIPSEAL_FROM_HOST, t, n, sealed, n + MLXR_CLIPSEAL_OVERHEAD - 1, &sl) == -1);
    /* the plain text of before is refused, not read */
    CHECK(mlxr_clipseal_open(key, MLXR_CLIPSEAL_FROM_HOST, (const uint8_t *)"0123456789012345678901234567890123456789", 40,
                             back, sizeof back, &tl) == -1);
    /* two seals of the same text differ (a fresh nonce each) */
    uint8_t s2[128];
    size_t sl2 = 0;
    CHECK(mlxr_clipseal_seal(key, MLXR_CLIPSEAL_FROM_HOST, t, n, sealed, sizeof sealed, &sl) == 0);
    CHECK(mlxr_clipseal_seal(key, MLXR_CLIPSEAL_FROM_HOST, t, n, s2, sizeof s2, &sl2) == 0);
    CHECK(sl == sl2 && memcmp(sealed, s2, sl) != 0);
}

static void test_key_line(void)
{
    uint8_t key[MLXR_CLIPSEAL_KEY_BYTES], back[MLXR_CLIPSEAL_KEY_BYTES];
    char line[MLXR_CLIPSEAL_KEY_LINE_BYTES + 1];
    CHECK(mlxr_clipseal_new_key(key) == 0);
    mlxr_clipseal_key_line(key, line);
    CHECK_EQ_INT(strlen(line), MLXR_CLIPSEAL_KEY_LINE_BYTES);
    CHECK(line[MLXR_CLIPSEAL_KEY_LINE_BYTES - 1] == '\n');
    CHECK(mlxr_clipseal_parse_key_line(line, strlen(line), back) == 0 && memcmp(key, back, sizeof key) == 0);
    CHECK(mlxr_clipseal_parse_key_line(line, strlen(line) - 1, back) == 0);   /* no newline */
    char crlf[MLXR_CLIPSEAL_KEY_LINE_BYTES + 2];
    memcpy(crlf, line, MLXR_CLIPSEAL_KEY_LINE_BYTES - 1);
    memcpy(crlf + MLXR_CLIPSEAL_KEY_LINE_BYTES - 1, "\r\n", 3);
    CHECK(mlxr_clipseal_parse_key_line(crlf, strlen(crlf), back) == 0);
    /* short, a bad digit, the wrong word, something after it */
    CHECK(mlxr_clipseal_parse_key_line(line, 20, back) == -1);
    char bad[MLXR_CLIPSEAL_KEY_LINE_BYTES + 8];
    memcpy(bad, line, sizeof line);
    bad[20] = 'g';
    CHECK(mlxr_clipseal_parse_key_line(bad, strlen(bad), back) == -1);
    memcpy(bad, line, sizeof line);
    bad[0] = 'X';
    CHECK(mlxr_clipseal_parse_key_line(bad, strlen(bad), back) == -1);
    memcpy(bad, line, sizeof line);
    memcpy(bad + MLXR_CLIPSEAL_KEY_LINE_BYTES - 1, "00\n", 4);
    CHECK(mlxr_clipseal_parse_key_line(bad, strlen(bad), back) == -1);
}

/* The overhead the user asked about: a full 1 MiB clipboard each way */
static void test_speed(void)
{
    uint8_t key[MLXR_CLIPSEAL_KEY_BYTES];
    CHECK(mlxr_clipseal_new_key(key) == 0);
    size_t n = MLXR_CLIPSEAL_MAX_TEXT, sl = 0, tl = 0;
    char *text = malloc(n + 1), *back = malloc(n + 1);
    uint8_t *sealed = malloc(n + MLXR_CLIPSEAL_OVERHEAD);
    for (size_t i = 0; i < n; i++) text[i] = (char)('a' + i % 26);
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    CHECK(mlxr_clipseal_seal(key, MLXR_CLIPSEAL_TO_HOST, text, n, sealed, n + MLXR_CLIPSEAL_OVERHEAD, &sl) == 0);
    CHECK(mlxr_clipseal_open(key, MLXR_CLIPSEAL_TO_HOST, sealed, sl, back, n + 1, &tl) == 0);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
    printf("clipseal: 1 MiB sealed and opened in %.2f ms\n", ms);
    CHECK(tl == n && memcmp(text, back, n) == 0);
    CHECK(mlxr_clipseal_seal(key, MLXR_CLIPSEAL_TO_HOST, text, n + 1, sealed, n + 1 + MLXR_CLIPSEAL_OVERHEAD, &sl) == -1);
    free(text);
    free(back);
    free(sealed);
}

int main(void)
{
    test_round_trip();
    test_refusals();
    test_key_line();
    test_speed();
    TEST_DONE();
}
