/*
 * vectordb.c - Implementation of the simple vector database
 */

#include "vectordb.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

void vdb_init(vdb_t *db, int dim) {
    memset(db, 0, sizeof(*db));
    db->dim = (dim > VDB_MAX_DIM) ? VDB_MAX_DIM : dim;
}

int vdb_insert(vdb_t *db, const char *id, const float *vec) {
    if (strlen(id) >= VDB_MAX_ID_LEN)
        return -1;

    /* Check for duplicate id and overwrite if found */
    for (int i = 0; i < VDB_MAX_ENTRIES; i++) {
        if (db->entries[i].active && strcmp(db->entries[i].id, id) == 0) {
            memcpy(db->entries[i].data, vec, db->dim * sizeof(float));
            return 0;
        }
    }

    /* Find a free slot */
    for (int i = 0; i < VDB_MAX_ENTRIES; i++) {
        if (!db->entries[i].active) {
            strncpy(db->entries[i].id, id, VDB_MAX_ID_LEN - 1);
            db->entries[i].id[VDB_MAX_ID_LEN - 1] = '\0';
            memcpy(db->entries[i].data, vec, db->dim * sizeof(float));
            db->entries[i].active = 1;
            db->count++;
            return 0;
        }
    }
    return -1; /* full */
}

int vdb_delete(vdb_t *db, const char *id) {
    for (int i = 0; i < VDB_MAX_ENTRIES; i++) {
        if (db->entries[i].active && strcmp(db->entries[i].id, id) == 0) {
            db->entries[i].active = 0;
            db->count--;
            return 0;
        }
    }
    return -1;
}

static float cosine_similarity(const float *a, const float *b, int dim) {
    float dot = 0.0f, mag_a = 0.0f, mag_b = 0.0f;
    for (int i = 0; i < dim; i++) {
        dot   += a[i] * b[i];
        mag_a += a[i] * a[i];
        mag_b += b[i] * b[i];
    }
    float denom = sqrtf(mag_a) * sqrtf(mag_b);
    if (denom < 1e-9f)
        return 0.0f;
    return dot / denom;
}

int vdb_search(const vdb_t *db, const float *query, int k,
               const char **out_ids, float *out_scores) {
    /* Brute-force search: score every active entry, keep top-k */
    int found = 0;

    for (int i = 0; i < VDB_MAX_ENTRIES; i++) {
        if (!db->entries[i].active)
            continue;

        float score = cosine_similarity(query, db->entries[i].data, db->dim);

        /* Insert into sorted results (descending by score) */
        int pos = found;
        if (found < k) {
            found++;
        } else if (score <= out_scores[k - 1]) {
            continue; /* worse than all current top-k */
        } else {
            pos = k - 1; /* will replace the worst */
        }

        /* Shift down to make room */
        while (pos > 0 && score > out_scores[pos - 1]) {
            if (pos < k) {
                out_ids[pos]    = out_ids[pos - 1];
                out_scores[pos] = out_scores[pos - 1];
            }
            pos--;
        }
        out_ids[pos]    = db->entries[i].id;
        out_scores[pos] = score;
    }

    return found;
}

int vdb_count(const vdb_t *db) {
    return db->count;
}

/* Binary file format:
 *   Header:  "VDB\0" (4 bytes), dim (int), count (int)
 *   Entries: id (VDB_MAX_ID_LEN bytes), data (dim * sizeof(float))
 * Only active entries are written.
 */

int vdb_save(const vdb_t *db, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    const char magic[4] = "VDB";
    if (fwrite(magic, 1, 4, f) != 4) goto fail;
    if (fwrite(&db->dim, sizeof(int), 1, f) != 1) goto fail;
    if (fwrite(&db->count, sizeof(int), 1, f) != 1) goto fail;

    for (int i = 0; i < VDB_MAX_ENTRIES; i++) {
        if (!db->entries[i].active) continue;
        if (fwrite(db->entries[i].id, 1, VDB_MAX_ID_LEN, f) != VDB_MAX_ID_LEN) goto fail;
        if (fwrite(db->entries[i].data, sizeof(float), db->dim, f) != (size_t)db->dim) goto fail;
    }

    fclose(f);
    return 0;

fail:
    fclose(f);
    return -1;
}

int vdb_load(vdb_t *db, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "VDB", 4) != 0) {
        fclose(f);
        return -1;
    }

    int dim, count;
    if (fread(&dim, sizeof(int), 1, f) != 1) { fclose(f); return -1; }
    if (fread(&count, sizeof(int), 1, f) != 1) { fclose(f); return -1; }

    vdb_init(db, dim);

    char id[VDB_MAX_ID_LEN];
    float data[VDB_MAX_DIM];
    for (int i = 0; i < count; i++) {
        if (fread(id, 1, VDB_MAX_ID_LEN, f) != VDB_MAX_ID_LEN) { fclose(f); return -1; }
        if (fread(data, sizeof(float), dim, f) != (size_t)dim) { fclose(f); return -1; }
        vdb_insert(db, id, data);
    }

    fclose(f);
    return 0;
}
