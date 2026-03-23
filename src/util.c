/*
 * util.c - Utility functions for the Racecar Vector Database
 */
#include "racecar.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

/* ----------------------------------------------------------------
 * rc_mkdir_p  –  recursively create directories (like mkdir -p)
 * Returns 0 on success, -1 on failure.
 * ---------------------------------------------------------------- */
int rc_mkdir_p(const char *path)
{
    char tmp[RC_MAX_PATH];
    size_t len;

    if (!path || !*path)
        return -1;

    len = strlen(path);
    if (len >= RC_MAX_PATH)
        return -1;

    memcpy(tmp, path, len + 1);

    /* Strip trailing slash (unless root "/") */
    if (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

/* ----------------------------------------------------------------
 * rc_data_dir  –  return the data directory path
 *
 * Checks $RACECAR_DATA first, otherwise $HOME/.racecar.
 * Creates the directory if it doesn't exist.
 * ---------------------------------------------------------------- */
const char *rc_data_dir(void)
{
    static char buf[RC_MAX_PATH];
    static int  inited = 0;

    if (inited)
        return buf;

    const char *env = getenv("RACECAR_DATA");
    if (env && *env) {
        snprintf(buf, sizeof(buf), "%s", env);
    } else {
        const char *home = getenv("HOME");
        if (!home)
            home = "/tmp";
        snprintf(buf, sizeof(buf), "%s/.racecar", home);
    }

    rc_mkdir_p(buf);
    inited = 1;
    return buf;
}

/* ----------------------------------------------------------------
 * rc_status_str  –  human-readable name for a status code
 * ---------------------------------------------------------------- */
const char *rc_status_str(rc_status_t s)
{
    switch (s) {
    case RC_OK:            return "OK";
    case RC_ERR_IO:        return "I/O error";
    case RC_ERR_MEM:       return "Out of memory";
    case RC_ERR_NOT_FOUND: return "Not found";
    case RC_ERR_EXISTS:    return "Already exists";
    case RC_ERR_INVALID:   return "Invalid argument";
    case RC_ERR_CORRUPT:   return "Corrupt data";
    case RC_ERR_FULL:      return "Full";
    default:               return "Unknown error";
    }
}

/* ----------------------------------------------------------------
 * rc_free_list  –  free an array of heap-allocated strings
 * ---------------------------------------------------------------- */
void rc_free_list(char **list, int count)
{
    if (!list) return;
    for (int i = 0; i < count; i++)
        free(list[i]);
    free(list);
}

/* ----------------------------------------------------------------
 * rc_time_us  –  monotonic time in microseconds
 * ---------------------------------------------------------------- */
uint64_t rc_time_us(void)
{
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
#endif
    /* fallback */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}
