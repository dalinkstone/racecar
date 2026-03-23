/*
 * cli.c - Command-line interface for the vector database
 *
 * Each command loads the database from a file, performs the operation,
 * saves if needed, and outputs JSON to stdout.
 *
 * Usage:
 *   vectordb_cli insert <dbfile> <id> <v1,v2,...,vN>
 *   vectordb_cli search <dbfile> <k>  <v1,v2,...,vN>
 *   vectordb_cli delete <dbfile> <id>
 *   vectordb_cli count  <dbfile>
 *   vectordb_cli list   <dbfile>
 */

#include "vectordb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parse comma-separated floats into out[]. Returns dimension count. */
static int parse_vector(const char *str, float *out, int max_dim) {
    int dim = 0;
    char *copy = strdup(str);
    if (!copy) return 0;
    char *tok = strtok(copy, ",");
    while (tok && dim < max_dim) {
        out[dim++] = strtof(tok, NULL);
        tok = strtok(NULL, ",");
    }
    free(copy);
    return dim;
}

/* Load database from file, or initialize a new one if file doesn't exist */
static int load_or_init(vdb_t *db, const char *path, int dim) {
    if (vdb_load(db, path) == 0) return 0;
    /* File doesn't exist or is invalid — start fresh */
    vdb_init(db, dim);
    return 1; /* 1 = new database */
}

static void usage(void) {
    fprintf(stderr,
        "Usage:\n"
        "  vectordb_cli insert <dbfile> <id> <v1,v2,...>\n"
        "  vectordb_cli search <dbfile> <k>  <v1,v2,...>\n"
        "  vectordb_cli delete <dbfile> <id>\n"
        "  vectordb_cli count  <dbfile>\n"
        "  vectordb_cli list   <dbfile>\n");
}

static int cmd_insert(const char *dbfile, const char *id, const char *vecstr) {
    float vec[VDB_MAX_DIM];
    int dim = parse_vector(vecstr, vec, VDB_MAX_DIM);
    if (dim == 0) {
        printf("{\"status\":\"error\",\"message\":\"empty vector\"}\n");
        return 1;
    }

    vdb_t db;
    load_or_init(&db, dbfile, dim);

    if (db.dim != dim) {
        printf("{\"status\":\"error\",\"message\":\"dimension mismatch: db=%d vec=%d\"}\n", db.dim, dim);
        return 1;
    }

    if (vdb_insert(&db, id, vec) != 0) {
        printf("{\"status\":\"error\",\"message\":\"insert failed (full or id too long)\"}\n");
        return 1;
    }

    if (vdb_save(&db, dbfile) != 0) {
        printf("{\"status\":\"error\",\"message\":\"save failed\"}\n");
        return 1;
    }

    printf("{\"status\":\"ok\"}\n");
    return 0;
}

static int cmd_search(const char *dbfile, int k, const char *vecstr) {
    float vec[VDB_MAX_DIM];
    int dim = parse_vector(vecstr, vec, VDB_MAX_DIM);
    if (dim == 0) {
        printf("{\"status\":\"error\",\"message\":\"empty vector\"}\n");
        return 1;
    }

    vdb_t db;
    if (vdb_load(&db, dbfile) != 0) {
        printf("{\"status\":\"error\",\"message\":\"database not found\"}\n");
        return 1;
    }

    const char *ids[VDB_MAX_ENTRIES];
    float scores[VDB_MAX_ENTRIES];
    int n = vdb_search(&db, vec, k, ids, scores);

    printf("{\"status\":\"ok\",\"results\":[");
    for (int i = 0; i < n; i++) {
        if (i > 0) printf(",");
        printf("{\"id\":\"%s\",\"score\":%.6f}", ids[i], scores[i]);
    }
    printf("]}\n");
    return 0;
}

static int cmd_delete(const char *dbfile, const char *id) {
    vdb_t db;
    if (vdb_load(&db, dbfile) != 0) {
        printf("{\"status\":\"error\",\"message\":\"database not found\"}\n");
        return 1;
    }

    if (vdb_delete(&db, id) != 0) {
        printf("{\"status\":\"error\",\"message\":\"id not found\"}\n");
        return 1;
    }

    if (vdb_save(&db, dbfile) != 0) {
        printf("{\"status\":\"error\",\"message\":\"save failed\"}\n");
        return 1;
    }

    printf("{\"status\":\"ok\"}\n");
    return 0;
}

static int cmd_count(const char *dbfile) {
    vdb_t db;
    if (vdb_load(&db, dbfile) != 0) {
        /* No file = 0 entries */
        printf("{\"status\":\"ok\",\"count\":0}\n");
        return 0;
    }
    printf("{\"status\":\"ok\",\"count\":%d}\n", vdb_count(&db));
    return 0;
}

static int cmd_list(const char *dbfile) {
    vdb_t db;
    if (vdb_load(&db, dbfile) != 0) {
        printf("{\"status\":\"ok\",\"ids\":[]}\n");
        return 0;
    }

    printf("{\"status\":\"ok\",\"ids\":[");
    int first = 1;
    for (int i = 0; i < VDB_MAX_ENTRIES; i++) {
        if (!db.entries[i].active) continue;
        if (!first) printf(",");
        printf("\"%s\"", db.entries[i].id);
        first = 0;
    }
    printf("]}\n");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage();
        return 1;
    }

    const char *cmd    = argv[1];
    const char *dbfile = argv[2];

    if (strcmp(cmd, "insert") == 0 && argc == 5) {
        return cmd_insert(dbfile, argv[3], argv[4]);
    } else if (strcmp(cmd, "search") == 0 && argc == 5) {
        return cmd_search(dbfile, atoi(argv[3]), argv[4]);
    } else if (strcmp(cmd, "delete") == 0 && argc == 4) {
        return cmd_delete(dbfile, argv[3]);
    } else if (strcmp(cmd, "count") == 0 && argc == 3) {
        return cmd_count(dbfile);
    } else if (strcmp(cmd, "list") == 0 && argc == 3) {
        return cmd_list(dbfile);
    } else {
        usage();
        return 1;
    }
}
