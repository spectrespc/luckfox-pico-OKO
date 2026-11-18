/*
 * Host test for the sleep heartbeat helpers (heartbeat.c).
 *
 * Covers the endpoint derivation ("frame" -> "heartbeat"), the proof-of-life
 * header block, the interval gate and the failure streak.  No HAL, media or
 * network code is needed.
 *
 * Run from the repository root:
 *   gcc -std=c99 -Wall -Wextra -O2 \
 *       -Iproject/app/divinus/source/src \
 *       project/app/divinus/tests/heartbeat_test.c \
 *       project/app/divinus/source/src/heartbeat.c \
 *       -o /tmp/heartbeat_test && /tmp/heartbeat_test
 *
 * Expected output:
 *   ok derive /ingest/frame
 *   ok derive /ingest
 *   ok derive /ingest/
 *   ok derive myframe is not a segment
 *   ok derive empty url
 *   ok derive small buffer
 *   ok headers order and values
 *   ok headers X-Heartbeat
 *   ok headers timestamp
 *   ...
 *   ok headers area -1
 *   ok interval 0 every cycle
 *   ok interval never gated
 *   ok interval gate
 *   ok interval gate reached
 *   ok interval clock jump
 *   ok streak reset and increment
 *   ok streak first failure
 *   ok streak grows
 *   ok streak negative
 *   ok streak capped
 *   all heartbeat helpers passed
 */
#include <stdio.h>
#include <string.h>

#include "heartbeat.h"

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", name);
        failures++;
    } else {
        printf("ok %s\n", name);
    }
}

static void test_derive(void)
{
    char url[64];

    heartbeat_derive_url(url, sizeof(url), "/ingest/frame");
    check(!strcmp(url, "/ingest/heartbeat"), "derive /ingest/frame");

    heartbeat_derive_url(url, sizeof(url), "/ingest");
    check(!strcmp(url, "/ingest/heartbeat"), "derive /ingest");

    heartbeat_derive_url(url, sizeof(url), "/ingest/");
    check(!strcmp(url, "/ingest/heartbeat"), "derive /ingest/");

    /* Only a whole trailing segment is the event endpoint. */
    heartbeat_derive_url(url, sizeof(url), "/ingest/myframe");
    check(!strcmp(url, "/ingest/myframe/heartbeat"),
        "derive myframe is not a segment");

    heartbeat_derive_url(url, sizeof(url), "");
    check(url[0] == 0, "derive empty url");

    /* "/frame" (6) -> "/heartbeat" (10) does not fit into 9 bytes. */
    heartbeat_derive_url(url, 9, "/frame");
    check(url[0] == 0, "derive small buffer");
}

static void test_headers(void)
{
    char headers[320];
    size_t used;

    used = heartbeat_build_headers(headers, sizeof(headers), 1700000000123ull,
        42, 7, 15);
    check(used == strlen(headers) && used > 0, "headers order and values");
    check(!strncmp(headers, "X-Heartbeat: 1\r\n", 16), "headers X-Heartbeat");
    check(strstr(headers, "X-Heartbeat-Ts: 1700000000123\r\n") != NULL,
        "headers timestamp");
    check(strstr(headers, "X-OKO-Idle-S: 42\r\n") != NULL, "headers idle");
    check(strstr(headers, "X-OKO-Motion-Area: 7\r\n") != NULL,
        "headers area");
    check(strstr(headers, "X-OKO-Sleep: quiet\r\n") != NULL,
        "headers sleep");
    check(strstr(headers, "X-OKO-Wake-Cycle: 15\r\n") != NULL,
        "headers wake cycle");
    check(used >= 2 && !strcmp(headers + used - 2, "\r\n"),
        "headers end in CRLF");

    heartbeat_build_headers(headers, sizeof(headers), 1, 0, -1, 0);
    check(strstr(headers, "X-OKO-Motion-Area: -1\r\n") != NULL,
        "headers area -1");
}

static void test_interval(void)
{
    check(heartbeat_interval_due(100, 100, 0), "interval 0 every cycle");
    check(heartbeat_interval_due(100, 0, 60), "interval never gated");
    check(!heartbeat_interval_due(100, 41, 60), "interval gate");
    check(heartbeat_interval_due(100, 40, 60), "interval gate reached");
    /* The gate must not fire after a clock jump backwards. */
    check(!heartbeat_interval_due(10, 500, 60), "interval clock jump");
}

static void test_streak(void)
{
    check(heartbeat_streak_next(0, 1) == 0, "streak reset and increment");
    check(heartbeat_streak_next(0, 0) == 1, "streak first failure");
    check(heartbeat_streak_next(4, 0) == 5, "streak grows");
    check(heartbeat_streak_next(-3, 0) == 1, "streak negative");
    check(heartbeat_streak_next(1000000, 0) == 1000000, "streak capped");
}

int main(void)
{
    test_derive();
    test_headers();
    test_interval();
    test_streak();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all heartbeat helpers passed\n");
    return 0;
}