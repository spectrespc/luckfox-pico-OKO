#include "ec_lock.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

static int lock_fd = -1;

static const char *lock_path(void)
{
    if (access("/run", W_OK) == 0)
        return "/run/oko-ec.lock";
    return "/tmp/oko-ec.lock";
}

int ec_lock_take(int blocking)
{
    if (lock_fd >= 0)
        return 0;
    lock_fd = open(lock_path(), O_RDWR | O_CREAT, 0666);
    if (lock_fd < 0)
        return 0;              /* not fatal: the daemon still polls */
    if (flock(lock_fd, LOCK_EX | (blocking ? 0 : LOCK_NB)) < 0)
    {
        close(lock_fd);
        lock_fd = -1;
        return -1;
    }
    return 0;
}

void ec_lock_release(void)
{
    if (lock_fd >= 0)
    {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        lock_fd = -1;
    }
}
