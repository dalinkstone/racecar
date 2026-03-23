/*
 * db.c - Database (directory-level) management for Racecar
 */
#include "racecar.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

/* ---- helpers ---- */

/* Validate a database / table name: not empty, no slashes, <= RC_MAX_NAME */
static int name_valid(const char *name)
{
    if (!name || !*name)
        return 0;
    if (strlen(name) > RC_MAX_NAME)
        return 0;
    for (const char *p = name; *p; p++) {
        if (*p == '/')
            return 0;
    }
    return 1;
}

/* Build the full path for a database directory into caller-supplied buf */
static void db_path(char *buf, size_t bufsz, const char *name)
{
    snprintf(buf, bufsz, "%s/%s", rc_data_dir(), name);
}

/* Compare function for qsort of strings */
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* ----------------------------------------------------------------
 * rc_db_create  –  create a new database directory
 * ---------------------------------------------------------------- */
rc_status_t rc_db_create(const char *name)
{
    if (!name_valid(name))
        return RC_ERR_INVALID;

    char path[RC_MAX_PATH];
    db_path(path, sizeof(path), name);

    struct stat st;
    if (stat(path, &st) == 0)
        return RC_ERR_EXISTS;

    if (rc_mkdir_p(path) != 0)
        return RC_ERR_IO;

    return RC_OK;
}

/* ----------------------------------------------------------------
 * rc_db_drop  –  remove a database directory and all its files
 * ---------------------------------------------------------------- */
rc_status_t rc_db_drop(const char *name)
{
    if (!name_valid(name))
        return RC_ERR_INVALID;

    char path[RC_MAX_PATH];
    db_path(path, sizeof(path), name);

    DIR *d = opendir(path);
    if (!d)
        return RC_ERR_NOT_FOUND;

    struct dirent *ent;
    char fpath[RC_MAX_PATH];
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        snprintf(fpath, sizeof(fpath), "%s/%s", path, ent->d_name);
        unlink(fpath);
    }
    closedir(d);

    if (rmdir(path) != 0)
        return RC_ERR_IO;

    return RC_OK;
}

/* ----------------------------------------------------------------
 * rc_db_list  –  list all database directories (sorted)
 * ---------------------------------------------------------------- */
rc_status_t rc_db_list(char ***out_names, int *out_count)
{
    if (!out_names || !out_count)
        return RC_ERR_INVALID;

    *out_names = NULL;
    *out_count = 0;

    const char *data = rc_data_dir();
    DIR *d = opendir(data);
    if (!d)
        return RC_ERR_IO;

    int cap = 16;
    int count = 0;
    char **names = malloc((size_t)cap * sizeof(char *));
    if (!names) {
        closedir(d);
        return RC_ERR_MEM;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;

        /* Check that it's actually a directory */
        char fpath[RC_MAX_PATH];
        snprintf(fpath, sizeof(fpath), "%s/%s", data, ent->d_name);
        struct stat st;
        if (stat(fpath, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        if (count == cap) {
            cap *= 2;
            char **tmp = realloc(names, (size_t)cap * sizeof(char *));
            if (!tmp) {
                rc_free_list(names, count);
                closedir(d);
                return RC_ERR_MEM;
            }
            names = tmp;
        }
        names[count] = strdup(ent->d_name);
        if (!names[count]) {
            rc_free_list(names, count);
            closedir(d);
            return RC_ERR_MEM;
        }
        count++;
    }
    closedir(d);

    if (count > 1)
        qsort(names, (size_t)count, sizeof(char *), cmp_str);

    *out_names = names;
    *out_count = count;
    return RC_OK;
}

/* ----------------------------------------------------------------
 * rc_db_exists  –  check if a database directory exists
 * ---------------------------------------------------------------- */
rc_status_t rc_db_exists(const char *name)
{
    if (!name_valid(name))
        return RC_ERR_INVALID;

    char path[RC_MAX_PATH];
    db_path(path, sizeof(path), name);

    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return RC_OK;

    return RC_ERR_NOT_FOUND;
}
