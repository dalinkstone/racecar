/*
 * vectordb.h - Simple in-memory vector database
 *
 * Stores named vectors of fixed dimension and supports
 * nearest-neighbor search via cosine similarity.
 */

#ifndef VECTORDB_H
#define VECTORDB_H

#include <stddef.h>

#define VDB_MAX_ENTRIES  1024
#define VDB_MAX_ID_LEN   64
#define VDB_MAX_DIM      512

/* A single stored vector */
typedef struct {
    char   id[VDB_MAX_ID_LEN];
    float  data[VDB_MAX_DIM];
    int    active;  /* 1 if slot is in use */
} vdb_entry_t;

/* The database */
typedef struct {
    vdb_entry_t entries[VDB_MAX_ENTRIES];
    int         dim;    /* dimension of all vectors */
    int         count;  /* number of active entries */
} vdb_t;

/* Initialize a database for vectors of the given dimension */
void vdb_init(vdb_t *db, int dim);

/* Insert a vector. Returns 0 on success, -1 if full or id too long */
int vdb_insert(vdb_t *db, const char *id, const float *vec);

/* Delete a vector by id. Returns 0 on success, -1 if not found */
int vdb_delete(vdb_t *db, const char *id);

/* Search for the k nearest neighbors of query.
 * Fills out_ids (array of char pointers) and out_scores with results.
 * Returns the number of results found (may be < k if db has fewer entries). */
int vdb_search(const vdb_t *db, const float *query, int k,
               const char **out_ids, float *out_scores);

/* Return the number of active entries */
int vdb_count(const vdb_t *db);

/* Save the database to a binary file. Returns 0 on success, -1 on error */
int vdb_save(const vdb_t *db, const char *path);

/* Load a database from a binary file. Returns 0 on success, -1 on error */
int vdb_load(vdb_t *db, const char *path);

#endif /* VECTORDB_H */
