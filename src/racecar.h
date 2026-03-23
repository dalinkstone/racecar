/*
 * racecar.h - Master header for the Racecar Vector Database
 *
 * A hyper-fast, pure-C vector database with HNSW indexing,
 * built-in text vectorization, and CLI interface.
 */
#ifndef RACECAR_H
#define RACECAR_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ================================================================
 * Constants
 * ================================================================ */
#define RC_MAGIC           0x52435242u   /* "RCRB" */
#define RC_MAGIC_IDX       0x52435849u   /* "RCXI" */
#define RC_VERSION         1
#define RC_MAX_NAME        63
#define RC_MAX_PATH        1024
#define RC_MAX_DIM         4096
#define RC_DEFAULT_META_SIZE    1024
#define RC_DEFAULT_CAPACITY     1024
#define RC_GROW_FACTOR          2
#define RC_DEFAULT_M            16
#define RC_DEFAULT_EF_CONSTRUCT 200
#define RC_DEFAULT_EF_SEARCH    50
#define RC_DEFAULT_TOP_K        10
#define RC_HNSW_MAX_LEVEL       32
#define RC_RECORD_ACTIVE        0x01

/* ================================================================
 * Status Codes
 * ================================================================ */
typedef enum {
    RC_OK           =  0,
    RC_ERR_IO       = -1,
    RC_ERR_MEM      = -2,
    RC_ERR_NOT_FOUND= -3,
    RC_ERR_EXISTS   = -4,
    RC_ERR_INVALID  = -5,
    RC_ERR_CORRUPT  = -6,
    RC_ERR_FULL     = -7
} rc_status_t;

/* ================================================================
 * Distance Metrics
 * ================================================================ */
typedef enum {
    RC_METRIC_COSINE    = 0,
    RC_METRIC_EUCLIDEAN = 1,
    RC_METRIC_DOT       = 2
} rc_metric_t;

/* ================================================================
 * On-Disk Table File Header (64 bytes)
 *
 * Layout:
 *   [magic:4][version:4][dimensions:4][metric:4]
 *   [meta_size:4][_pad:4][record_count:8]
 *   [next_id:8][capacity:8][_reserved:16]
 * ================================================================ */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t dimensions;
    uint32_t metric;
    uint32_t meta_size;
    uint32_t _pad0;
    uint64_t record_count;
    uint64_t next_id;
    uint64_t capacity;
    uint8_t  _reserved[16];
} rc_file_header_t;

/*
 * On-disk record layout (per row, packed):
 *   [id : 8 bytes, uint64_t]
 *   [flags : 4 bytes, uint32_t]  (RC_RECORD_ACTIVE = live)
 *   [meta_len : 4 bytes, uint32_t]
 *   [vector : dimensions * 4 bytes, float[]]
 *   [metadata : meta_size bytes, char[]]
 *
 * record_size = 16 + dimensions*4 + meta_size
 *
 * Access helpers (ptr = base of record):
 *   id       = *(uint64_t*)(ptr)
 *   flags    = *(uint32_t*)(ptr + 8)
 *   meta_len = *(uint32_t*)(ptr + 12)
 *   vector   = (float*)(ptr + 16)
 *   metadata = (char*)(ptr + 16 + dim*4)
 */

/* ================================================================
 * In-Memory Table Handle
 * ================================================================ */
typedef struct {
    FILE            *fp;
    rc_file_header_t header;
    uint8_t         *records;        /* in-memory record buffer */
    size_t           records_size;   /* used bytes */
    size_t           records_cap;    /* allocated bytes */
    uint32_t         record_size;    /* 16 + dim*4 + meta_size */
    char             path[RC_MAX_PATH];
    int              dirty;
} rc_table_t;

/* ================================================================
 * Record (in-memory, user-facing)
 * ================================================================ */
typedef struct {
    uint64_t id;
    uint32_t flags;
    uint32_t meta_len;
    const float *vector;       /* pointer into table buffer */
    const char  *metadata;     /* pointer into table buffer */
} rc_record_t;

/* ================================================================
 * Search Result
 * ================================================================ */
typedef struct {
    uint64_t id;
    float    distance;
} rc_result_t;

/* ================================================================
 * HNSW Index (opaque)
 * ================================================================ */
typedef struct rc_hnsw rc_hnsw_t;

/* ================================================================
 * Minimal JSON Value
 * ================================================================ */
typedef enum {
    RC_JSON_NULL,
    RC_JSON_BOOL,
    RC_JSON_NUMBER,
    RC_JSON_STRING,
    RC_JSON_ARRAY,
    RC_JSON_OBJECT
} rc_json_type_t;

typedef struct rc_json_value {
    rc_json_type_t type;
    union {
        int        boolean;
        double     number;
        struct { char *str; size_t len; }                          string;
        struct { struct rc_json_value **items; size_t count; }     array;
        struct { char **keys; struct rc_json_value **vals; size_t count; } object;
    } v;
} rc_json_t;

/* ================================================================
 * Function Declarations
 * ================================================================ */

/* ---- util.c ---- */
const char *rc_data_dir(void);            /* ~/.racecar or $RACECAR_DATA */
const char *rc_status_str(rc_status_t s);
void        rc_free_list(char **list, int count);
int         rc_mkdir_p(const char *path);
uint64_t    rc_time_us(void);              /* monotonic microseconds */

/* ---- db.c ---- */
rc_status_t rc_db_create(const char *name);
rc_status_t rc_db_drop(const char *name);
rc_status_t rc_db_list(char ***out_names, int *out_count);
rc_status_t rc_db_exists(const char *name);

/* ---- table.c ---- */
rc_table_t *rc_table_create(const char *db, const char *name,
                            uint32_t dim, rc_metric_t metric,
                            uint32_t meta_size);
rc_table_t *rc_table_open(const char *db, const char *name);
void        rc_table_close(rc_table_t *t);
rc_status_t rc_table_drop(const char *db, const char *name);
rc_status_t rc_table_list(const char *db, char ***out_names, int *out_count);
rc_status_t rc_table_flush(rc_table_t *t);

rc_status_t rc_table_insert(rc_table_t *t, const float *vec,
                            const char *meta, uint64_t *id_out);
rc_status_t rc_table_get(rc_table_t *t, uint64_t id, rc_record_t *rec);
rc_status_t rc_table_delete(rc_table_t *t, uint64_t id);

/* Brute-force scan (flat search) */
rc_status_t rc_table_scan(rc_table_t *t, const float *query,
                          uint32_t top_k,
                          rc_result_t *results, uint32_t *result_count);

/* ---- vector.c ---- */
float  rc_vec_dot(const float *a, const float *b, uint32_t dim);
float  rc_vec_norm(const float *v, uint32_t dim);
void   rc_vec_normalize(float *v, uint32_t dim);
float  rc_vec_cosine_dist(const float *a, const float *b, uint32_t dim);
float  rc_vec_euclidean_dist(const float *a, const float *b, uint32_t dim);
float  rc_vec_distance(const float *a, const float *b, uint32_t dim, rc_metric_t m);

/* ---- hnsw.c ---- */
rc_hnsw_t  *rc_hnsw_create(uint32_t dim, rc_metric_t metric,
                            uint32_t m, uint32_t ef_construction);
void        rc_hnsw_free(rc_hnsw_t *idx);
rc_status_t rc_hnsw_insert(rc_hnsw_t *idx, uint64_t id, const float *vec);
rc_status_t rc_hnsw_search(rc_hnsw_t *idx, const float *query,
                            uint32_t top_k, uint32_t ef_search,
                            rc_result_t *results, uint32_t *result_count);
rc_status_t rc_hnsw_save(rc_hnsw_t *idx, const char *path);
rc_hnsw_t  *rc_hnsw_load(const char *path);
rc_status_t rc_hnsw_build_from_table(rc_table_t *t,
                                      uint32_t m, uint32_t ef_construction,
                                      const char *index_path);

/* ---- json.c ---- */
rc_json_t  *rc_json_parse(const char *str);
void        rc_json_free(rc_json_t *v);
const char *rc_json_get_str(rc_json_t *obj, const char *key);
double      rc_json_get_num(rc_json_t *obj, const char *key);
rc_json_t  *rc_json_get(rc_json_t *obj, const char *key);
int         rc_json_dump(rc_json_t *v, char *buf, size_t bufsize);

/* ---- tokenizer.c ---- */
rc_status_t rc_vectorize_text(const char *text, float *out_vec, uint32_t dim);
rc_status_t rc_vectorize_json(const char *json_str, float *out_vec, uint32_t dim);

#endif /* RACECAR_H */
