/*
 * Host test for the YOLO inference history used by /api/yolo?detections=1.
 *
 * Run:
 *   gcc -std=c99 -Wall -Wextra -O2 \
 *       -Iproject/app/divinus/source/src/analytics \
 *       -o /tmp/yolo_history_test \
 *       project/app/divinus/tests/yolo_history_test.c \
 *       project/app/divinus/source/src/analytics/yolo_history.c -lpthread &&
 *       /tmp/yolo_history_test
 *
 * Expected output: one "ok ..." line per case, no "FAIL".
 */
#include <stdio.h>
#include <string.h>

#include "yolo_history.h"

static int failures;

static void check(int condition, const char *name)
{
    if (condition) {
        printf("ok %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        failures++;
    }
}

static yolo_history_det make_det(int cls, int x)
{
    yolo_history_det det;

    memset(&det, 0, sizeof(det));
    det.cls = (int16_t)cls;
    det.score = 32768;
    det.x = (uint16_t)x;
    det.y = 100;
    det.w = 200;
    det.h = 300;
    return det;
}

int main(void)
{
    yolo_history_record out[YOLO_HISTORY_RECORDS];
    yolo_history_det dets[12];
    uint32_t last = 77;
    int i, n;

    for (i = 0; i < 12; i++) dets[i] = make_det(i, i * 10);

    yolo_history_reset();
    n = yolo_history_copy(0, out, YOLO_HISTORY_RECORDS, &last);
    check(n == 0 && last == 0, "empty ring");

    check(yolo_history_push(1000, 1, 0, dets, 2) == 1, "first seq is 1");
    check(yolo_history_push(2000, 2, 2, dets, 1) == 2, "seq increments");
    check(yolo_history_push(3000, 1, 0, dets, 0) == 3, "empty frame stored");

    n = yolo_history_copy(0, out, YOLO_HISTORY_RECORDS, &last);
    check(n == 3 && last == 3, "copy everything");
    check(out[0].seq == 1 && out[2].seq == 3, "oldest first");
    check(out[0].ts == 1000 && out[1].source == 2 && out[1].aiisp == 2,
        "metadata preserved");
    check(out[0].count == 2 && out[0].det[1].x == 10, "detections preserved");
    check(out[2].count == 0, "zero detections preserved");

    n = yolo_history_copy(2, out, YOLO_HISTORY_RECORDS, &last);
    check(n == 1 && out[0].seq == 3 && last == 3, "incremental copy");

    n = yolo_history_copy(3, out, YOLO_HISTORY_RECORDS, &last);
    check(n == 0 && last == 3, "cursor at the tail");

    n = yolo_history_copy(1000, out, YOLO_HISTORY_RECORDS, &last);
    check(n == 0 && last == 3, "cursor ahead of the ring");

    yolo_history_reset();
    for (i = 1; i <= YOLO_HISTORY_RECORDS + 6; i++)
        yolo_history_push((uint64_t)i, 1, 0, dets, 1);

    n = yolo_history_copy(0, out, YOLO_HISTORY_RECORDS, &last);
    check(n == YOLO_HISTORY_RECORDS, "ring is bounded");
    check(out[0].seq == 7 && out[n - 1].seq == 70, "ring keeps the newest");

    n = yolo_history_copy(0, out, 10, &last);
    check(n == 10 && out[0].seq == 7 && out[9].seq == 16 && last == 16,
        "truncated copy returns a usable cursor");

    n = yolo_history_copy(out[9].seq, out, YOLO_HISTORY_RECORDS, &last);
    check(n == YOLO_HISTORY_RECORDS - 10 && last == 70,
        "resume after a truncated copy");

    check(yolo_history_push(100, 1, 0, dets, 12) == 71, "overlong push accepted");
    n = yolo_history_copy(70, out, YOLO_HISTORY_RECORDS, &last);
    check(n == 1 && out[0].count == YOLO_HISTORY_DETS,
        "detections capped at eight");
    check(out[0].det[7].cls == 7, "strongest detections kept");

    check(yolo_history_push(0, 1, 0, NULL, 1) == 0, "NULL detections rejected");
    check(yolo_history_push(0, 1, 0, dets, -1) == 0, "negative count rejected");

    yolo_history_reset();
    n = yolo_history_copy(0, out, YOLO_HISTORY_RECORDS, &last);
    check(n == 0 && last == 0, "reset clears the ring");

    return failures ? 1 : 0;
}