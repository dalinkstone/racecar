/*
 * main.c - CLI entry point for the Racecar Vector Database
 *
 * Usage: racecar <command> [arguments...]
 */

#include "racecar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* ================================================================
 * Helpers
 * ================================================================ */

static const char *metric_name(rc_metric_t m)
{
    switch (m) {
    case RC_METRIC_COSINE:    return "cosine";
    case RC_METRIC_EUCLIDEAN: return "euclidean";
    case RC_METRIC_DOT:       return "dot";
    default:                  return "unknown";
    }
}

static rc_metric_t parse_metric(const char *s)
{
    if (!s) return RC_METRIC_COSINE;
    if (strcmp(s, "cosine") == 0 || strcmp(s, "cos") == 0)
        return RC_METRIC_COSINE;
    if (strcmp(s, "euclidean") == 0 || strcmp(s, "euc") == 0 || strcmp(s, "l2") == 0)
        return RC_METRIC_EUCLIDEAN;
    if (strcmp(s, "dot") == 0)
        return RC_METRIC_DOT;
    return RC_METRIC_COSINE;
}

/*
 * Parse a comma-separated float string like "1.0,2.0,3.0" into out_vec.
 * Returns the number of floats parsed.
 */
static int parse_vector(const char *csv, float *out_vec, uint32_t max_dim)
{
    int count = 0;
    const char *p = csv;

    while (*p && (uint32_t)count < max_dim) {
        char *end;
        float val = strtof(p, &end);
        if (end == p) break;           /* no conversion */
        out_vec[count++] = val;
        if (*end == ',') end++;
        p = end;
    }
    return count;
}

/* ================================================================
 * Usage / Help
 * ================================================================ */

static void print_help(void)
{
    printf(
        "Usage: racecar <command> [arguments...]\n"
        "\n"
        "Database Commands:\n"
        "  db-create <name>                                Create a new database\n"
        "  db-list                                         List all databases\n"
        "  db-drop <name>                                  Drop a database\n"
        "\n"
        "Table Commands:\n"
        "  table-create <db> <name> <dim> [metric]         Create table (metric: cosine|euclidean|dot)\n"
        "  table-list <db>                                 List tables in database\n"
        "  table-info <db> <table>                         Show table information\n"
        "  table-drop <db> <table>                         Drop a table\n"
        "\n"
        "Record Commands:\n"
        "  insert <db> <table> <vector_csv> [metadata]     Insert a vector (comma-separated floats)\n"
        "  get <db> <table> <id>                           Get record by ID\n"
        "  delete <db> <table> <id>                        Delete record by ID\n"
        "\n"
        "Search Commands:\n"
        "  search <db> <table> <vector_csv> [top_k]        Brute-force search (default top_k=10)\n"
        "  index-build <db> <table> [M] [ef]               Build HNSW index (defaults: M=16, ef=200)\n"
        "  index-search <db> <table> <vector_csv> [top_k] [ef]\n"
        "                                                  Search using HNSW index\n"
        "\n"
        "Vectorization:\n"
        "  vectorize <text> <dim>                          Convert text to vector\n"
        "  vectorize-json <json_string> <dim>              Convert JSON to vector\n"
        "\n"
        "Bulk Operations:\n"
        "  bulk-insert <db> <table> <jsonl_file>           Bulk insert from JSONL file\n"
        "  export <db> <table>                             Export table as JSONL to stdout\n"
        "\n"
        "Utility:\n"
        "  bench <db> <table> <count> <dim>                Run insert+search benchmark\n"
        "  version                                         Show version\n"
        "  help                                            Show this help\n"
    );
}

/* ================================================================
 * Command Implementations
 * ================================================================ */

static int cmd_db_create(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: racecar db-create <name>\n");
        return 1;
    }
    rc_status_t s = rc_db_create(argv[2]);
    if (s == RC_OK)
        printf("Database '%s' created.\n", argv[2]);
    else
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
    return s != RC_OK;
}

static int cmd_db_list(void)
{
    char **names = NULL;
    int count = 0;
    rc_status_t s = rc_db_list(&names, &count);
    if (s != RC_OK) {
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
        return 1;
    }
    if (count == 0) {
        printf("No databases found.\n");
    } else {
        for (int i = 0; i < count; i++)
            printf("  %s\n", names[i]);
    }
    rc_free_list(names, count);
    return 0;
}

static int cmd_db_drop(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: racecar db-drop <name>\n");
        return 1;
    }
    rc_status_t s = rc_db_drop(argv[2]);
    if (s == RC_OK)
        printf("Dropped database '%s'.\n", argv[2]);
    else
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
    return s != RC_OK;
}

static int cmd_table_create(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "Usage: racecar table-create <db> <name> <dim> [metric]\n");
        return 1;
    }
    const char *db   = argv[2];
    const char *name = argv[3];
    uint32_t dim     = (uint32_t)strtoul(argv[4], NULL, 10);
    rc_metric_t met  = (argc > 5) ? parse_metric(argv[5]) : RC_METRIC_COSINE;

    if (dim == 0 || dim > RC_MAX_DIM) {
        fprintf(stderr, "Error: invalid dimension %u (max %d)\n", dim, RC_MAX_DIM);
        return 1;
    }

    rc_table_t *t = rc_table_create(db, name, dim, met, RC_DEFAULT_META_SIZE);
    if (!t) {
        fprintf(stderr, "Error: failed to create table '%s'\n", name);
        return 1;
    }
    printf("Table '%s' created (dim=%u, metric=%s).\n", name, dim, metric_name(met));
    rc_table_close(t);
    return 0;
}

static int cmd_table_list(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: racecar table-list <db>\n");
        return 1;
    }
    char **names = NULL;
    int count = 0;
    rc_status_t s = rc_table_list(argv[2], &names, &count);
    if (s != RC_OK) {
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
        return 1;
    }
    if (count == 0) {
        printf("No tables found.\n");
    } else {
        for (int i = 0; i < count; i++)
            printf("  %s\n", names[i]);
    }
    rc_free_list(names, count);
    return 0;
}

static int cmd_table_info(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "Usage: racecar table-info <db> <table>\n");
        return 1;
    }
    rc_table_t *t = rc_table_open(argv[2], argv[3]);
    if (!t) {
        fprintf(stderr, "Error: could not open table '%s' in db '%s'\n", argv[3], argv[2]);
        return 1;
    }
    printf("Table: %s\n", argv[3]);
    printf("Dimensions: %u\n", t->header.dimensions);
    printf("Metric: %s\n", metric_name((rc_metric_t)t->header.metric));
    printf("Records: %lu\n", (unsigned long)t->header.record_count);
    printf("Meta size: %u\n", t->header.meta_size);
    printf("Record size: %u bytes\n", t->record_size);
    rc_table_close(t);
    return 0;
}

static int cmd_table_drop(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "Usage: racecar table-drop <db> <table>\n");
        return 1;
    }
    rc_status_t s = rc_table_drop(argv[2], argv[3]);
    if (s == RC_OK)
        printf("Dropped table '%s'.\n", argv[3]);
    else
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
    return s != RC_OK;
}

static int cmd_insert(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "Usage: racecar insert <db> <table> <vector_csv> [metadata]\n");
        return 1;
    }
    rc_table_t *t = rc_table_open(argv[2], argv[3]);
    if (!t) {
        fprintf(stderr, "Error: could not open table '%s'\n", argv[3]);
        return 1;
    }

    float vec[RC_MAX_DIM];
    int ndim = parse_vector(argv[4], vec, RC_MAX_DIM);
    if ((uint32_t)ndim != t->header.dimensions) {
        fprintf(stderr, "Error: expected %u dimensions, got %d\n",
                t->header.dimensions, ndim);
        rc_table_close(t);
        return 1;
    }

    const char *meta = (argc > 5) ? argv[5] : "";
    uint64_t id = 0;
    rc_status_t s = rc_table_insert(t, vec, meta, &id);
    if (s == RC_OK)
        printf("Inserted record ID=%lu\n", (unsigned long)id);
    else
        fprintf(stderr, "Error: %s\n", rc_status_str(s));

    rc_table_close(t);
    return s != RC_OK;
}

static int cmd_get(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "Usage: racecar get <db> <table> <id>\n");
        return 1;
    }
    rc_table_t *t = rc_table_open(argv[2], argv[3]);
    if (!t) {
        fprintf(stderr, "Error: could not open table '%s'\n", argv[3]);
        return 1;
    }

    uint64_t id = strtoull(argv[4], NULL, 10);
    rc_record_t rec;
    rc_status_t s = rc_table_get(t, id, &rec);
    if (s != RC_OK) {
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
        rc_table_close(t);
        return 1;
    }

    printf("ID: %lu\n", (unsigned long)rec.id);
    printf("Vector: ");
    for (uint32_t i = 0; i < t->header.dimensions; i++) {
        if (i > 0) printf(",");
        printf("%.6g", rec.vector[i]);
    }
    printf("\n");
    if (rec.metadata && rec.meta_len > 0)
        printf("Metadata: %.*s\n", (int)rec.meta_len, rec.metadata);

    rc_table_close(t);
    return 0;
}

static int cmd_delete(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "Usage: racecar delete <db> <table> <id>\n");
        return 1;
    }
    rc_table_t *t = rc_table_open(argv[2], argv[3]);
    if (!t) {
        fprintf(stderr, "Error: could not open table '%s'\n", argv[3]);
        return 1;
    }

    uint64_t id = strtoull(argv[4], NULL, 10);
    rc_status_t s = rc_table_delete(t, id);
    if (s == RC_OK)
        printf("Deleted record ID=%lu\n", (unsigned long)id);
    else
        fprintf(stderr, "Error: %s\n", rc_status_str(s));

    rc_table_close(t);
    return s != RC_OK;
}

static void print_results(const rc_result_t *results, uint32_t count, double elapsed_ms)
{
    printf("Search completed in %.3f ms\n\n", elapsed_ms);
    printf("%-6s  %-10s  %s\n", "Rank", "ID", "Distance");
    for (uint32_t i = 0; i < count; i++) {
        printf("%-6u  %-10lu  %.4f\n",
               i + 1, (unsigned long)results[i].id, results[i].distance);
    }
    if (count == 0) printf("No results.\n");
}

static int cmd_search(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "Usage: racecar search <db> <table> <vector_csv> [top_k]\n");
        return 1;
    }
    rc_table_t *t = rc_table_open(argv[2], argv[3]);
    if (!t) {
        fprintf(stderr, "Error: could not open table '%s'\n", argv[3]);
        return 1;
    }

    float vec[RC_MAX_DIM];
    int ndim = parse_vector(argv[4], vec, RC_MAX_DIM);
    if ((uint32_t)ndim != t->header.dimensions) {
        fprintf(stderr, "Error: expected %u dimensions, got %d\n",
                t->header.dimensions, ndim);
        rc_table_close(t);
        return 1;
    }

    uint32_t top_k = (argc > 5) ? (uint32_t)strtoul(argv[5], NULL, 10) : RC_DEFAULT_TOP_K;
    if (top_k == 0) top_k = RC_DEFAULT_TOP_K;

    rc_result_t results[top_k];
    uint32_t result_count = 0;

    uint64_t t0 = rc_time_us();
    rc_status_t s = rc_table_scan(t, vec, top_k, results, &result_count);
    uint64_t t1 = rc_time_us();

    if (s != RC_OK) {
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
        rc_table_close(t);
        return 1;
    }

    double elapsed_ms = (double)(t1 - t0) / 1000.0;
    print_results(results, result_count, elapsed_ms);

    rc_table_close(t);
    return 0;
}

static int cmd_index_build(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "Usage: racecar index-build <db> <table> [M] [ef]\n");
        return 1;
    }
    rc_table_t *t = rc_table_open(argv[2], argv[3]);
    if (!t) {
        fprintf(stderr, "Error: could not open table '%s'\n", argv[3]);
        return 1;
    }

    uint32_t m  = (argc > 4) ? (uint32_t)strtoul(argv[4], NULL, 10) : RC_DEFAULT_M;
    uint32_t ef = (argc > 5) ? (uint32_t)strtoul(argv[5], NULL, 10) : RC_DEFAULT_EF_CONSTRUCT;
    if (m == 0) m = RC_DEFAULT_M;
    if (ef == 0) ef = RC_DEFAULT_EF_CONSTRUCT;

    /* Build index path: replace .rct with .rcx */
    char idx_path[RC_MAX_PATH];
    strncpy(idx_path, t->path, RC_MAX_PATH - 1);
    idx_path[RC_MAX_PATH - 1] = '\0';
    size_t len = strlen(idx_path);
    if (len >= 4 && strcmp(idx_path + len - 4, ".rct") == 0) {
        idx_path[len - 3] = 'r';
        idx_path[len - 2] = 'c';
        idx_path[len - 1] = 'x';
    } else {
        strncat(idx_path, ".rcx", RC_MAX_PATH - len - 1);
    }

    printf("Building HNSW index (M=%u, ef_construction=%u)...\n", m, ef);

    uint64_t t0 = rc_time_us();
    rc_status_t s = rc_hnsw_build_from_table(t, m, ef, idx_path);
    uint64_t t1 = rc_time_us();

    if (s == RC_OK) {
        double elapsed_ms = (double)(t1 - t0) / 1000.0;
        printf("Index built in %.3f ms\n", elapsed_ms);
        printf("Saved to: %s\n", idx_path);
    } else {
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
    }

    rc_table_close(t);
    return s != RC_OK;
}

static int cmd_index_search(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "Usage: racecar index-search <db> <table> <vector_csv> [top_k] [ef]\n");
        return 1;
    }

    /* Open table to get dimensions and build index path */
    rc_table_t *t = rc_table_open(argv[2], argv[3]);
    if (!t) {
        fprintf(stderr, "Error: could not open table '%s'\n", argv[3]);
        return 1;
    }
    uint32_t dim = t->header.dimensions;

    /* Build index path from table path */
    char idx_path[RC_MAX_PATH];
    strncpy(idx_path, t->path, RC_MAX_PATH - 1);
    idx_path[RC_MAX_PATH - 1] = '\0';
    size_t plen = strlen(idx_path);
    if (plen >= 4 && strcmp(idx_path + plen - 4, ".rct") == 0) {
        idx_path[plen - 3] = 'r';
        idx_path[plen - 2] = 'c';
        idx_path[plen - 1] = 'x';
    }
    rc_table_close(t);

    /* Load index */
    rc_hnsw_t *idx = rc_hnsw_load(idx_path);
    if (!idx) {
        fprintf(stderr, "Error: could not load index '%s'\n", idx_path);
        return 1;
    }

    float vec[RC_MAX_DIM];
    int ndim = parse_vector(argv[4], vec, RC_MAX_DIM);
    if ((uint32_t)ndim != dim) {
        fprintf(stderr, "Error: expected %u dimensions, got %d\n", dim, ndim);
        rc_hnsw_free(idx);
        return 1;
    }

    uint32_t top_k     = (argc > 5) ? (uint32_t)strtoul(argv[5], NULL, 10) : RC_DEFAULT_TOP_K;
    uint32_t ef_search = (argc > 6) ? (uint32_t)strtoul(argv[6], NULL, 10) : RC_DEFAULT_EF_SEARCH;
    if (top_k == 0) top_k = RC_DEFAULT_TOP_K;
    if (ef_search == 0) ef_search = RC_DEFAULT_EF_SEARCH;

    rc_result_t results[top_k];
    uint32_t result_count = 0;

    uint64_t t0 = rc_time_us();
    rc_status_t s = rc_hnsw_search(idx, vec, top_k, ef_search, results, &result_count);
    uint64_t t1 = rc_time_us();

    if (s != RC_OK) {
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
        rc_hnsw_free(idx);
        return 1;
    }

    double elapsed_ms = (double)(t1 - t0) / 1000.0;
    print_results(results, result_count, elapsed_ms);

    rc_hnsw_free(idx);
    return 0;
}

static int cmd_vectorize(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "Usage: racecar vectorize <text> <dim>\n");
        return 1;
    }
    uint32_t dim = (uint32_t)strtoul(argv[3], NULL, 10);
    if (dim == 0 || dim > RC_MAX_DIM) {
        fprintf(stderr, "Error: invalid dimension %u\n", dim);
        return 1;
    }

    float *vec = malloc(dim * sizeof(float));
    if (!vec) {
        fprintf(stderr, "Error: out of memory\n");
        return 1;
    }

    rc_status_t s = rc_vectorize_text(argv[2], vec, dim);
    if (s != RC_OK) {
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
        free(vec);
        return 1;
    }

    for (uint32_t i = 0; i < dim; i++) {
        if (i > 0) printf(",");
        printf("%.6g", vec[i]);
    }
    printf("\n");

    free(vec);
    return 0;
}

static int cmd_vectorize_json(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "Usage: racecar vectorize-json <json_string> <dim>\n");
        return 1;
    }
    uint32_t dim = (uint32_t)strtoul(argv[3], NULL, 10);
    if (dim == 0 || dim > RC_MAX_DIM) {
        fprintf(stderr, "Error: invalid dimension %u\n", dim);
        return 1;
    }

    float *vec = malloc(dim * sizeof(float));
    if (!vec) {
        fprintf(stderr, "Error: out of memory\n");
        return 1;
    }

    rc_status_t s = rc_vectorize_json(argv[2], vec, dim);
    if (s != RC_OK) {
        fprintf(stderr, "Error: %s\n", rc_status_str(s));
        free(vec);
        return 1;
    }

    for (uint32_t i = 0; i < dim; i++) {
        if (i > 0) printf(",");
        printf("%.6g", vec[i]);
    }
    printf("\n");

    free(vec);
    return 0;
}

static int cmd_bulk_insert(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "Usage: racecar bulk-insert <db> <table> <jsonl_file>\n");
        return 1;
    }
    rc_table_t *t = rc_table_open(argv[2], argv[3]);
    if (!t) {
        fprintf(stderr, "Error: could not open table '%s'\n", argv[3]);
        return 1;
    }

    FILE *fp = fopen(argv[4], "r");
    if (!fp) {
        fprintf(stderr, "Error: could not open file '%s'\n", argv[4]);
        rc_table_close(t);
        return 1;
    }

    uint32_t dim = t->header.dimensions;
    float *vec = malloc(dim * sizeof(float));
    if (!vec) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(fp);
        rc_table_close(t);
        return 1;
    }

    char *line = malloc(65536);
    if (!line) {
        fprintf(stderr, "Error: out of memory\n");
        free(vec);
        fclose(fp);
        rc_table_close(t);
        return 1;
    }

    uint64_t t0 = rc_time_us();
    int inserted = 0;
    int errors = 0;
    int line_num = 0;

    while (fgets(line, 65536, fp)) {
        line_num++;
        /* Strip trailing newline */
        size_t ln = strlen(line);
        while (ln > 0 && (line[ln - 1] == '\n' || line[ln - 1] == '\r'))
            line[--ln] = '\0';
        if (ln == 0) continue;

        rc_json_t *root = rc_json_parse(line);
        if (!root || root->type != RC_JSON_OBJECT) {
            fprintf(stderr, "Warning: skipping invalid JSON on line %d\n", line_num);
            if (root) rc_json_free(root);
            errors++;
            continue;
        }

        /* Extract vector array */
        rc_json_t *vec_arr = rc_json_get(root, "vector");
        if (!vec_arr || vec_arr->type != RC_JSON_ARRAY) {
            fprintf(stderr, "Warning: missing 'vector' array on line %d\n", line_num);
            rc_json_free(root);
            errors++;
            continue;
        }
        if (vec_arr->v.array.count != dim) {
            fprintf(stderr, "Warning: wrong dimension on line %d (expected %u, got %zu)\n",
                    line_num, dim, vec_arr->v.array.count);
            rc_json_free(root);
            errors++;
            continue;
        }
        for (uint32_t i = 0; i < dim; i++) {
            rc_json_t *item = vec_arr->v.array.items[i];
            vec[i] = (item && item->type == RC_JSON_NUMBER) ? (float)item->v.number : 0.0f;
        }

        /* Extract metadata */
        char meta_buf[RC_DEFAULT_META_SIZE];
        meta_buf[0] = '\0';
        rc_json_t *meta_val = rc_json_get(root, "metadata");
        if (meta_val) {
            rc_json_dump(meta_val, meta_buf, sizeof(meta_buf));
        }

        uint64_t id = 0;
        rc_status_t s = rc_table_insert(t, vec, meta_buf, &id);
        if (s == RC_OK) {
            inserted++;
        } else {
            fprintf(stderr, "Warning: insert failed on line %d: %s\n",
                    line_num, rc_status_str(s));
            errors++;
        }

        rc_json_free(root);
    }

    uint64_t t1 = rc_time_us();
    double elapsed_ms = (double)(t1 - t0) / 1000.0;

    printf("Bulk insert complete: %d records in %.3f ms", inserted, elapsed_ms);
    if (errors > 0)
        printf(" (%d errors)", errors);
    printf("\n");

    free(line);
    free(vec);
    fclose(fp);
    rc_table_close(t);
    return 0;
}

static int cmd_export(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "Usage: racecar export <db> <table>\n");
        return 1;
    }
    rc_table_t *t = rc_table_open(argv[2], argv[3]);
    if (!t) {
        fprintf(stderr, "Error: could not open table '%s'\n", argv[3]);
        return 1;
    }

    uint32_t dim = t->header.dimensions;
    uint64_t count = t->header.record_count;

    /* Iterate all possible record slots */
    uint64_t next_id = t->header.next_id;
    for (uint64_t id = 0; id < next_id; id++) {
        rc_record_t rec;
        rc_status_t s = rc_table_get(t, id, &rec);
        if (s != RC_OK) continue;
        if (!(rec.flags & RC_RECORD_ACTIVE)) continue;

        printf("{\"id\": %lu, \"vector\": [", (unsigned long)rec.id);
        for (uint32_t i = 0; i < dim; i++) {
            if (i > 0) printf(", ");
            printf("%.6g", rec.vector[i]);
        }
        printf("]");

        if (rec.metadata && rec.meta_len > 0) {
            printf(", \"metadata\": %.*s", (int)rec.meta_len, rec.metadata);
        }
        printf("}\n");
    }
    (void)count;

    rc_table_close(t);
    return 0;
}

static int cmd_bench(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "Usage: racecar bench <db> <table> <count> <dim>\n");
        return 1;
    }
    const char *db   = argv[2];
    const char *name = argv[3];
    int count        = atoi(argv[4]);
    uint32_t dim     = (uint32_t)strtoul(argv[5], NULL, 10);

    if (count <= 0 || dim == 0 || dim > RC_MAX_DIM) {
        fprintf(stderr, "Error: invalid count or dimension\n");
        return 1;
    }

    /* Create table */
    rc_table_t *t = rc_table_create(db, name, dim, RC_METRIC_COSINE, RC_DEFAULT_META_SIZE);
    if (!t) {
        fprintf(stderr, "Error: could not create table '%s'\n", name);
        return 1;
    }

    float *vec = malloc(dim * sizeof(float));
    if (!vec) {
        fprintf(stderr, "Error: out of memory\n");
        rc_table_close(t);
        rc_table_drop(db, name);
        return 1;
    }

    /* Benchmark: insert */
    printf("Inserting %d records (dim=%u)...\n", count, dim);
    uint64_t t_ins0 = rc_time_us();
    for (int i = 0; i < count; i++) {
        for (uint32_t d = 0; d < dim; d++)
            vec[d] = (float)rand() / (float)RAND_MAX;
        uint64_t id;
        rc_status_t s = rc_table_insert(t, vec, "", &id);
        if (s != RC_OK) {
            fprintf(stderr, "Error on insert %d: %s\n", i, rc_status_str(s));
            break;
        }
    }
    uint64_t t_ins1 = rc_time_us();
    double ins_ms = (double)(t_ins1 - t_ins0) / 1000.0;
    double recs_per_sec = (ins_ms > 0.0) ? (count / (ins_ms / 1000.0)) : 0.0;

    /* Benchmark: flat search */
    for (uint32_t d = 0; d < dim; d++)
        vec[d] = (float)rand() / (float)RAND_MAX;

    rc_result_t results[RC_DEFAULT_TOP_K];
    uint32_t result_count = 0;

    uint64_t t_s0 = rc_time_us();
    rc_table_scan(t, vec, RC_DEFAULT_TOP_K, results, &result_count);
    uint64_t t_s1 = rc_time_us();
    double scan_ms = (double)(t_s1 - t_s0) / 1000.0;

    /* Benchmark: HNSW index (if enough records) */
    double idx_build_ms = 0.0;
    double idx_search_ms = 0.0;
    int did_index = 0;

    if (count >= 100) {
        did_index = 1;

        /* Build index path */
        char idx_path[RC_MAX_PATH];
        strncpy(idx_path, t->path, RC_MAX_PATH - 1);
        idx_path[RC_MAX_PATH - 1] = '\0';
        size_t plen = strlen(idx_path);
        if (plen >= 4 && strcmp(idx_path + plen - 4, ".rct") == 0) {
            idx_path[plen - 3] = 'r';
            idx_path[plen - 2] = 'c';
            idx_path[plen - 1] = 'x';
        }

        printf("Building HNSW index...\n");
        uint64_t t_ib0 = rc_time_us();
        rc_status_t s = rc_hnsw_build_from_table(t, RC_DEFAULT_M, RC_DEFAULT_EF_CONSTRUCT, idx_path);
        uint64_t t_ib1 = rc_time_us();
        idx_build_ms = (double)(t_ib1 - t_ib0) / 1000.0;

        if (s == RC_OK) {
            /* Index search */
            rc_hnsw_t *idx = rc_hnsw_load(idx_path);
            if (idx) {
                rc_result_t idx_results[RC_DEFAULT_TOP_K];
                uint32_t idx_count = 0;

                uint64_t t_is0 = rc_time_us();
                rc_hnsw_search(idx, vec, RC_DEFAULT_TOP_K, RC_DEFAULT_EF_SEARCH,
                               idx_results, &idx_count);
                uint64_t t_is1 = rc_time_us();
                idx_search_ms = (double)(t_is1 - t_is0) / 1000.0;

                rc_hnsw_free(idx);
            }
            /* Clean up index file */
            remove(idx_path);
        } else {
            fprintf(stderr, "Warning: index build failed: %s\n", rc_status_str(s));
            did_index = 0;
        }
    }

    printf("\nBenchmark Results:\n");
    printf("  Records:     %d\n", count);
    printf("  Dimensions:  %u\n", dim);
    printf("  Insert:      %.3f ms (%.0f rec/s)\n", ins_ms, recs_per_sec);
    printf("  Flat search: %.3f ms\n", scan_ms);
    if (did_index) {
        printf("  Index build: %.3f ms\n", idx_build_ms);
        printf("  Index search:%.3f ms\n", idx_search_ms);
    }

    free(vec);
    rc_table_close(t);
    rc_table_drop(db, name);
    return 0;
}

/* ================================================================
 * Main Dispatch
 * ================================================================ */

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_help();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "db-create") == 0)
        return cmd_db_create(argc, argv);
    else if (strcmp(cmd, "db-list") == 0)
        return cmd_db_list();
    else if (strcmp(cmd, "db-drop") == 0)
        return cmd_db_drop(argc, argv);
    else if (strcmp(cmd, "table-create") == 0)
        return cmd_table_create(argc, argv);
    else if (strcmp(cmd, "table-list") == 0)
        return cmd_table_list(argc, argv);
    else if (strcmp(cmd, "table-info") == 0)
        return cmd_table_info(argc, argv);
    else if (strcmp(cmd, "table-drop") == 0)
        return cmd_table_drop(argc, argv);
    else if (strcmp(cmd, "insert") == 0)
        return cmd_insert(argc, argv);
    else if (strcmp(cmd, "get") == 0)
        return cmd_get(argc, argv);
    else if (strcmp(cmd, "delete") == 0)
        return cmd_delete(argc, argv);
    else if (strcmp(cmd, "search") == 0)
        return cmd_search(argc, argv);
    else if (strcmp(cmd, "index-build") == 0)
        return cmd_index_build(argc, argv);
    else if (strcmp(cmd, "index-search") == 0)
        return cmd_index_search(argc, argv);
    else if (strcmp(cmd, "vectorize") == 0)
        return cmd_vectorize(argc, argv);
    else if (strcmp(cmd, "vectorize-json") == 0)
        return cmd_vectorize_json(argc, argv);
    else if (strcmp(cmd, "bulk-insert") == 0)
        return cmd_bulk_insert(argc, argv);
    else if (strcmp(cmd, "export") == 0)
        return cmd_export(argc, argv);
    else if (strcmp(cmd, "bench") == 0)
        return cmd_bench(argc, argv);
    else if (strcmp(cmd, "version") == 0) {
        printf("racecar v0.1.0 — Hyper-fast vector database\n");
        return 0;
    }
    else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_help();
        return 0;
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_help();
        return 1;
    }
}
