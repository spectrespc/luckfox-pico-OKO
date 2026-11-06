/*
 * oko-flash - thin dispatcher for controller firmware updates.
 *
 * The HTTP flash worker runs this utility, so the actual protocol code stays
 * in oko-ec (STM32) and oko-motor (STM8).  Standard output is passed through
 * unchanged; the worker parses "Progress: ... NN%" (oko-ec) and
 * "PROGRESS nn" (oko-motor).
 *
 * Exit codes: 0 success, 1 flash error, 2 usage or blocked.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: oko-flash --target stm32|stm8 --file PATH [--version HEX]\n"
        "\n"
        "  stm32  requires --version, a 32-bit hex value (e.g. 0x00010001)\n"
        "  stm8   FILE is app.ihx/.hex or app.bin produced by mkapp.py\n");
}

static const char *find_tool(const char *name, char *buffer, size_t size)
{
    const char *env = NULL;
    const char *base;

    if (!strcmp(name, "oko-ec"))
        env = getenv("OKO_EC_BIN");
    else if (!strcmp(name, "oko-motor"))
        env = getenv("OKO_MOTOR_BIN");
    if (env && *env)
        return env;

    if (access("/oem/usr/bin/", F_OK) == 0)
    {
        snprintf(buffer, size, "/oem/usr/bin/%s", name);
        if (access(buffer, X_OK) == 0)
            return buffer;
    }

    /* Fall back to the directory the dispatcher itself was started from. */
    {
        char self[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (n > 0)
        {
            self[n] = '\0';
            base = dirname(self);
            snprintf(buffer, size, "%s/%s", base, name);
            if (access(buffer, X_OK) == 0)
                return buffer;
        }
    }

    snprintf(buffer, size, "%s", name);
    return buffer;
}

static int run(char *const argv[])
{
    pid_t pid = fork();
    int status = 0;

    if (pid < 0)
    {
        fprintf(stderr, "ERROR fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0)
    {
        execvp(argv[0], argv);
        fprintf(stderr, "ERROR exec %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            fprintf(stderr, "ERROR waitpid: %s\n", strerror(errno));
            return 1;
        }
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status) == 0 ? 0 : 1;
    fprintf(stderr, "ERROR %s terminated by signal %d\n", argv[0],
            WIFSIGNALED(status) ? WTERMSIG(status) : -1);
    return 1;
}

static int valid_version(const char *text)
{
    char *end = NULL;
    unsigned long value;

    if (!text || strncmp(text, "0x", 2) != 0)
        return 0;
    value = strtoul(text, &end, 16);
    if (!end || *end || value > 0xFFFFFFFFul)
        return 0;
    return 1;
}

int main(int argc, char **argv)
{
    const char *target = NULL;
    const char *file = NULL;
    const char *version = NULL;
    char ec_path[PATH_MAX], motor_path[PATH_MAX];
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--target") && i + 1 < argc)
            target = argv[++i];
        else if (!strcmp(argv[i], "--file") && i + 1 < argc)
            file = argv[++i];
        else if (!strcmp(argv[i], "--version") && i + 1 < argc)
            version = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            usage();
            return 0;
        }
        else
        {
            usage();
            return 2;
        }
    }

    if (!target || !file)
    {
        usage();
        return 2;
    }
    if (access(file, R_OK))
    {
        fprintf(stderr, "ERROR cannot read %s: %s\n", file, strerror(errno));
        return 2;
    }

    if (!strcmp(target, "stm32"))
    {
        char *args[5];

        if (!valid_version(version))
        {
            fprintf(stderr, "ERROR stm32 needs --version 0xXXXXXXXX\n");
            return 2;
        }
        args[0] = (char *)find_tool("oko-ec", ec_path, sizeof(ec_path));
        args[1] = "flash";
        args[2] = (char *)file;
        args[3] = (char *)version;
        args[4] = NULL;
        return run(args);
    }

    if (!strcmp(target, "stm8"))
    {
        char *args[4];

        args[0] = (char *)find_tool("oko-motor", motor_path,
                                    sizeof(motor_path));
        args[1] = "flash";
        args[2] = (char *)file;
        args[3] = NULL;
        return run(args);
    }

    fprintf(stderr, "ERROR unknown target %s\n", target);
    return 2;
}