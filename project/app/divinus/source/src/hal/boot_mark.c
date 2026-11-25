#include "boot_mark.h"

#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static unsigned long long bootMarkLastUs;

static unsigned long long bootMarkNowUs(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts))
        return 0;
    return (unsigned long long)ts.tv_sec * 1000000ULL +
        (unsigned long long)ts.tv_nsec / 1000;
}

void boot_mark(const char *stage) {
    unsigned long long now = bootMarkNowUs();
    unsigned long long delta = bootMarkLastUs && now >= bootMarkLastUs ?
        now - bootMarkLastUs : 0;
    char line[96];
    int fd, length;

    bootMarkLastUs = now;

    length = snprintf(line, sizeof(line), "divinus-boot: %s dt=%lluus\n",
        stage ? stage : "?", delta);
    if (length <= 0)
        return;
    if (length >= (int)sizeof(line))
        length = (int)sizeof(line) - 1;

    fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0)
        return;
    if (write(fd, line, length) < 0) {
        /* No readable kernel log: the marker is skipped, not fatal. */
    }
    close(fd);
}