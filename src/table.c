/*
 * table.c - Core storage engine for the Racecar Vector Database
 *
 * Tables are .rct files with an in-memory buffered record array.
 * This file implements create, open, close, flush, insert, get,
 * delete, and brute-force nearest-neighbour scan.
 */
#include "racecar.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <dirent.h>
#include <errno.h>

/* ---- internal helpers ---- */

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

static void table_path(char *buf, size_t bufsz,
                       const char *db, const char *name)
{
    snprintf(buf, bufsz, "%s/%s/%s.rct", rc_data_dir(), db, name);
}

static void index_path(char *buf, size_t bufsz,
                       const char *db, const char *name)
{
    snprintf(buf, bufsz, "%s/%s/%s.rcx", rc_data_dir(), db, name);
}

/* Pointer to the start of record at index i */
static inline uint8_t *rec_ptr(const rc_table_t *t, uint64_t i)
{
    return t->records + i * t->record_size;
}

/* Access helpers for a record pointer */
static inline uint64_t rec_id(const uint8_t *p)      { uint64_t v; memcpy(&v, p, 8); return v; }
static inline uint32_t rec_flags(const uint8_t *p)    { uint32_t v; memcpy(&v, p + 8, 4); return v; }
static inline uint32_t rec_meta_len(const uint8_t *p) { uint32_t v; memcpy(&v, p + 12, 4); return v; }
static inline const float *rec_vec(const uint8_t *p)  { return (const float *)(p + 16); }
static inline const char  *rec_meta(const uint8_t *p, uint32_t dim)
{
    return (const char *)(p + 16 + dim * sizeof(float));
}

static inline void rec_set_id(uint8_t *p, uint64_t id)        { memcpy(p, &id, 8); }
static inline void rec_set_flags(uint8_t *p, uint32_t f)      { memcpy(p + 8, &f, 4); }
static inline void rec_set_meta_len(uint8_t *p, uint32_t ml)  { memcpy(p + 12, &ml, 4); }

/* Compare for qsort on strings */
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Compare for qsort on rc_result_t by distance ascending */
static int cmp_result(const void *a, const void *b)
{
    float da = ((const rc_result_t *)a)->distance;
    float db = ((const rc_result_t *)b)->distance;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

/* ================================================================
 * rc_table_create
 * ================================================================ */
rc_table_t *rc_table_create(const char *db, const char *name,
                            uint32_t dim, rc_metric_t metric,
                            uint32_t meta_size)
{
    if (!name_valid(name) || !name_valid(db))
        return NULL;
    if (dim == 0 || dim > RC_MAX_DIM)
        return NULL;
    if (rc_db_exists(db) != RC_OK)
        return NULL;
    if (meta_size == 0)
        meta_size = RC_DEFAULT_META_SIZE;

    char path[RC_MAX_PATH];
    table_path(path, sizeof(path), db, name);

    /* Refuse to overwrite */
    {
        FILE *test = fopen(path, "rb");
        if (test) {
            fclose(test);
            return NULL;
        }
    }

    rc_table_t *t = calloc(1, sizeof(rc_table_t));
    if (!t) return NULL;

    t->record_size = 16 + dim * (uint32_t)sizeof(float) + meta_size;
    memcpy(t->path, path, strlen(path) + 1);

    /* Header */
    t->header.magic        = RC_MAGIC;
    t->header.version      = RC_VERSION;
    t->header.dimensions   = dim;
    t->header.metric       = (uint32_t)metric;
    t->header.meta_size    = meta_size;
    t->header._pad0        = 0;
    t->header.record_count = 0;
    t->header.next_id      = 1;
    t->header.capacity     = RC_DEFAULT_CAPACITY;
    memset(t->header._reserved, 0, sizeof(t->header._reserved));

    /* Allocate record buffer */
    size_t buf_bytes = (size_t)t->header.capacity * t->record_size;
    t->records = calloc(1, buf_bytes);
    if (!t->records) {
        free(t);
        return NULL;
    }
    t->records_cap  = buf_bytes;
    t->records_size = 0;

    /* Write initial file: header only (no records yet) */
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(t->records);
        free(t);
        return NULL;
    }
    if (fwrite(&t->header, sizeof(rc_file_header_t), 1, fp) != 1) {
        fclose(fp);
        free(t->records);
        free(t);
        return NULL;
    }
    fclose(fp);

    /* Re-open for read/write */
    t->fp = fopen(path, "r+b");
    if (!t->fp) {
        free(t->records);
        free(t);
        return NULL;
    }
    t->dirty = 0;

    return t;
}

/* ================================================================
 * rc_table_open
 * ================================================================ */
rc_table_t *rc_table_open(const char *db, const char *name)
{
    if (!name_valid(name) || !name_valid(db))
        return NULL;

    char path[RC_MAX_PATH];
    table_path(path, sizeof(path), db, name);

    FILE *fp = fopen(path, "r+b");
    if (!fp)
        return NULL;

    rc_file_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        fclose(fp);
        return NULL;
    }

    if (hdr.magic != RC_MAGIC || hdr.version != RC_VERSION) {
        fclose(fp);
        return NULL;
    }

    rc_table_t *t = calloc(1, sizeof(rc_table_t));
    if (!t) {
        fclose(fp);
        return NULL;
    }

    t->fp     = fp;
    t->header = hdr;
    memcpy(t->path, path, strlen(path) + 1);
    t->record_size = 16 + hdr.dimensions * (uint32_t)sizeof(float) + hdr.meta_size;

    /* Allocate record buffer for full capacity */
    size_t buf_bytes = (size_t)hdr.capacity * t->record_size;
    t->records = malloc(buf_bytes);
    if (!t->records) {
        fclose(fp);
        free(t);
        return NULL;
    }
    t->records_cap = buf_bytes;

    /* Read existing records */
    size_t data_bytes = (size_t)hdr.record_count * t->record_size;
    if (data_bytes > 0) {
        if (fread(t->records, 1, data_bytes, fp) != data_bytes) {
            free(t->records);
            fclose(fp);
            free(t);
            return NULL;
        }
    }
    t->records_size = data_bytes;
    t->dirty = 0;

    return t;
}

/* ================================================================
 * rc_table_flush
 * ================================================================ */
rc_status_t rc_table_flush(rc_table_t *t)
{
    if (!t || !t->fp)
        return RC_ERR_INVALID;

    if (fseek(t->fp, 0, SEEK_SET) != 0)
        return RC_ERR_IO;

    if (fwrite(&t->header, sizeof(rc_file_header_t), 1, t->fp) != 1)
        return RC_ERR_IO;

    size_t data_bytes = (size_t)t->header.record_count * t->record_size;
    if (data_bytes > 0) {
        if (fwrite(t->records, 1, data_bytes, t->fp) != data_bytes)
            return RC_ERR_IO;
    }

    fflush(t->fp);
    t->dirty = 0;
    return RC_OK;
}

/* ================================================================
 * rc_table_close
 * ================================================================ */
void rc_table_close(rc_table_t *t)
{
    if (!t) return;
    if (t->dirty)
        rc_table_flush(t);
    if (t->fp)
        fclose(t->fp);
    free(t->records);
    free(t);
}

/* ================================================================
 * rc_table_drop
 * ================================================================ */
rc_status_t rc_table_drop(const char *db, const char *name)
{
    if (!name_valid(name) || !name_valid(db))
        return RC_ERR_INVALID;

    char path[RC_MAX_PATH];
    table_path(path, sizeof(path), db, name);

    if (remove(path) != 0)
        return RC_ERR_NOT_FOUND;

    /* Also remove index file if it exists */
    char idx[RC_MAX_PATH];
    index_path(idx, sizeof(idx), db, name);
    remove(idx);  /* ignore errors – may not exist */

    return RC_OK;
}

/* ================================================================
 * rc_table_list
 * ================================================================ */
rc_status_t rc_table_list(const char *db, char ***out_names, int *out_count)
{
    if (!out_names || !out_count || !name_valid(db))
        return RC_ERR_INVALID;

    *out_names = NULL;
    *out_count = 0;

    char dir[RC_MAX_PATH];
    snprintf(dir, sizeof(dir), "%s/%s", rc_data_dir(), db);

    DIR *d = opendir(dir);
    if (!d)
        return RC_ERR_NOT_FOUND;

    int cap = 16;
    int count = 0;
    char **names = malloc((size_t)cap * sizeof(char *));
    if (!names) {
        closedir(d);
        return RC_ERR_MEM;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len < 5)
            continue;
        if (strcmp(ent->d_name + len - 4, ".rct") != 0)
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
        /* Strip the .rct extension */
        names[count] = malloc(len - 3);  /* len-4 chars + NUL */
        if (!names[count]) {
            rc_free_list(names, count);
            closedir(d);
            return RC_ERR_MEM;
        }
        memcpy(names[count], ent->d_name, len - 4);
        names[count][len - 4] = '\0';
        count++;
    }
    closedir(d);

    if (count > 1)
        qsort(names, (size_t)count, sizeof(char *), cmp_str);

    *out_names = names;
    *out_count = count;
    return RC_OK;
}

/* ================================================================
 * rc_table_insert
 * ================================================================ */
rc_status_t rc_table_insert(rc_table_t *t, const float *vec,
                            const char *meta, uint64_t *id_out)
{
    if (!t || !vec)
        return RC_ERR_INVALID;

    /* Grow if at capacity */
    if (t->header.record_count >= t->header.capacity) {
        uint64_t new_cap = t->header.capacity * RC_GROW_FACTOR;
        size_t new_bytes = (size_t)new_cap * t->record_size;
        uint8_t *nb = realloc(t->records, new_bytes);
        if (!nb)
            return RC_ERR_MEM;
        /* Zero the new region */
        memset(nb + t->records_cap, 0, new_bytes - t->records_cap);
        t->records      = nb;
        t->records_cap  = new_bytes;
        t->header.capacity = new_cap;
    }

    uint8_t *p = rec_ptr(t, t->header.record_count);

    uint64_t id = t->header.next_id++;
    rec_set_id(p, id);
    rec_set_flags(p, RC_RECORD_ACTIVE);

    /* Vector */
    uint32_t dim = t->header.dimensions;
    memcpy(p + 16, vec, dim * sizeof(float));

    /* Metadata */
    uint32_t meta_len = 0;
    uint32_t ms = t->header.meta_size;
    char *meta_dst = (char *)(p + 16 + dim * sizeof(float));
    if (meta) {
        meta_len = (uint32_t)strlen(meta);
        if (meta_len >= ms)
            meta_len = ms - 1;
        memcpy(meta_dst, meta, meta_len);
    }
    meta_dst[meta_len] = '\0';
    /* Zero-pad remainder */
    if (meta_len + 1 < ms)
        memset(meta_dst + meta_len + 1, 0, ms - meta_len - 1);

    rec_set_meta_len(p, meta_len);

    t->header.record_count++;
    t->records_size = (size_t)t->header.record_count * t->record_size;
    t->dirty = 1;

    if (id_out)
        *id_out = id;

    return RC_OK;
}

/* ================================================================
 * rc_table_get
 * ================================================================ */
rc_status_t rc_table_get(rc_table_t *t, uint64_t id, rc_record_t *rec)
{
    if (!t || !rec)
        return RC_ERR_INVALID;

    uint32_t dim = t->header.dimensions;

    for (uint64_t i = 0; i < t->header.record_count; i++) {
        const uint8_t *p = rec_ptr(t, i);
        if (rec_id(p) == id && (rec_flags(p) & RC_RECORD_ACTIVE)) {
            rec->id       = id;
            rec->flags    = rec_flags(p);
            rec->meta_len = rec_meta_len(p);
            rec->vector   = rec_vec(p);
            rec->metadata = rec_meta(p, dim);
            return RC_OK;
        }
    }
    return RC_ERR_NOT_FOUND;
}

/* ================================================================
 * rc_table_delete
 * ================================================================ */
rc_status_t rc_table_delete(rc_table_t *t, uint64_t id)
{
    if (!t)
        return RC_ERR_INVALID;

    for (uint64_t i = 0; i < t->header.record_count; i++) {
        uint8_t *p = rec_ptr(t, i);
        if (rec_id(p) == id && (rec_flags(p) & RC_RECORD_ACTIVE)) {
            rec_set_flags(p, rec_flags(p) & ~(uint32_t)RC_RECORD_ACTIVE);
            t->dirty = 1;
            return rc_table_flush(t);
        }
    }
    return RC_ERR_NOT_FOUND;
}

/* ================================================================
 * rc_table_scan  –  brute-force nearest-neighbour search
 * ================================================================ */
rc_status_t rc_table_scan(rc_table_t *t, const float *query,
                          uint32_t top_k,
                          rc_result_t *results, uint32_t *result_count)
{
    if (!t || !query || !results || !result_count || top_k == 0)
        return RC_ERR_INVALID;

    uint32_t dim = t->header.dimensions;
    rc_metric_t metric = (rc_metric_t)t->header.metric;

    /* Initialise result slots */
    for (uint32_t i = 0; i < top_k; i++) {
        results[i].id       = 0;
        results[i].distance = 1e30f;
    }

    uint32_t found = 0;
    float worst_dist = 1e30f;
    uint32_t worst_idx = 0;

    for (uint64_t i = 0; i < t->header.record_count; i++) {
        const uint8_t *p = rec_ptr(t, i);
        if (!(rec_flags(p) & RC_RECORD_ACTIVE))
            continue;

        float dist = rc_vec_distance(query, rec_vec(p), dim, metric);

        if (found < top_k) {
            results[found].id       = rec_id(p);
            results[found].distance = dist;
            found++;
            /* Update worst */
            if (found == 1 || dist > worst_dist) {
                worst_dist = dist;
                worst_idx  = found - 1;
            }
            /* After filling all slots, find the true worst */
            if (found == top_k) {
                worst_dist = results[0].distance;
                worst_idx  = 0;
                for (uint32_t j = 1; j < top_k; j++) {
                    if (results[j].distance > worst_dist) {
                        worst_dist = results[j].distance;
                        worst_idx  = j;
                    }
                }
            }
        } else if (dist < worst_dist) {
            /* Replace the worst entry */
            results[worst_idx].id       = rec_id(p);
            results[worst_idx].distance = dist;
            /* Find new worst */
            worst_dist = results[0].distance;
            worst_idx  = 0;
            for (uint32_t j = 1; j < top_k; j++) {
                if (results[j].distance > worst_dist) {
                    worst_dist = results[j].distance;
                    worst_idx  = j;
                }
            }
        }
    }

    *result_count = found;

    /* Sort results by distance ascending */
    if (found > 1)
        qsort(results, found, sizeof(rc_result_t), cmp_result);

    return RC_OK;
}
