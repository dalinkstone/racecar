/*
 * test_vectordb.c - Tests for the vector database
 *
 * Compiles as a standalone binary. Returns 0 if all tests pass.
 */

#include "vectordb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do {                          \
    tests_run++;                                        \
    if (cond) {                                         \
        tests_passed++;                                 \
        printf("  PASS: %s\n", msg);                    \
    } else {                                            \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__);\
    }                                                   \
} while(0)

/* ---- Test: basic insert and count ---- */
static void test_insert_count(void) {
    printf("[test_insert_count]\n");
    vdb_t db;
    vdb_init(&db, 3);

    float v1[] = {1.0f, 0.0f, 0.0f};
    float v2[] = {0.0f, 1.0f, 0.0f};

    ASSERT(vdb_insert(&db, "a", v1) == 0, "insert a");
    ASSERT(vdb_insert(&db, "b", v2) == 0, "insert b");
    ASSERT(vdb_count(&db) == 2, "count is 2");
}

/* ---- Test: duplicate insert overwrites ---- */
static void test_duplicate_insert(void) {
    printf("[test_duplicate_insert]\n");
    vdb_t db;
    vdb_init(&db, 2);

    float v1[] = {1.0f, 0.0f};
    float v2[] = {0.0f, 1.0f};

    vdb_insert(&db, "x", v1);
    vdb_insert(&db, "x", v2); /* overwrite */
    ASSERT(vdb_count(&db) == 1, "count still 1 after overwrite");

    /* Search should find the updated vector */
    float query[] = {0.0f, 1.0f};
    const char *ids[1];
    float scores[1];
    int n = vdb_search(&db, query, 1, ids, scores);
    ASSERT(n == 1 && strcmp(ids[0], "x") == 0, "finds overwritten vector");
    ASSERT(scores[0] > 0.99f, "overwritten vector matches query");
}

/* ---- Test: delete ---- */
static void test_delete(void) {
    printf("[test_delete]\n");
    vdb_t db;
    vdb_init(&db, 3);

    float v[] = {1.0f, 2.0f, 3.0f};
    vdb_insert(&db, "del_me", v);
    ASSERT(vdb_count(&db) == 1, "count is 1 before delete");
    ASSERT(vdb_delete(&db, "del_me") == 0, "delete succeeds");
    ASSERT(vdb_count(&db) == 0, "count is 0 after delete");
    ASSERT(vdb_delete(&db, "del_me") == -1, "double delete returns -1");
}

/* ---- Test: nearest neighbor search ---- */
static void test_search(void) {
    printf("[test_search]\n");
    vdb_t db;
    vdb_init(&db, 3);

    /* Three orthogonal-ish vectors */
    float v1[] = {1.0f, 0.0f, 0.0f};
    float v2[] = {0.0f, 1.0f, 0.0f};
    float v3[] = {0.0f, 0.0f, 1.0f};

    vdb_insert(&db, "right", v1);
    vdb_insert(&db, "up",    v2);
    vdb_insert(&db, "fwd",   v3);

    /* Query close to "right" */
    float q[] = {0.9f, 0.1f, 0.0f};
    const char *ids[2];
    float scores[2];
    int n = vdb_search(&db, q, 2, ids, scores);

    ASSERT(n == 2, "returns 2 results");
    ASSERT(strcmp(ids[0], "right") == 0, "closest is 'right'");
    ASSERT(scores[0] > scores[1], "scores are descending");
}

/* ---- Test: search with k > count ---- */
static void test_search_fewer_than_k(void) {
    printf("[test_search_fewer_than_k]\n");
    vdb_t db;
    vdb_init(&db, 2);

    float v[] = {1.0f, 0.0f};
    vdb_insert(&db, "only", v);

    const char *ids[5];
    float scores[5];
    int n = vdb_search(&db, v, 5, ids, scores);
    ASSERT(n == 1, "returns only 1 when db has 1 entry");
    ASSERT(strcmp(ids[0], "only") == 0, "correct id");
}

/* ---- Test: cosine similarity correctness ---- */
static void test_cosine_values(void) {
    printf("[test_cosine_values]\n");
    vdb_t db;
    vdb_init(&db, 2);

    /* identical direction → score ~1.0 */
    float v[] = {3.0f, 4.0f};
    vdb_insert(&db, "same_dir", v);

    float q[] = {6.0f, 8.0f}; /* same direction, different magnitude */
    const char *ids[1];
    float scores[1];
    vdb_search(&db, q, 1, ids, scores);
    ASSERT(fabsf(scores[0] - 1.0f) < 0.001f, "parallel vectors → score ~1.0");

    /* opposite direction → score ~-1.0 */
    float v2[] = {-3.0f, -4.0f};
    vdb_insert(&db, "opposite", v2);

    const char *ids2[2];
    float scores2[2];
    vdb_search(&db, q, 2, ids2, scores2);
    ASSERT(fabsf(scores2[1] - (-1.0f)) < 0.001f, "antiparallel vectors → score ~-1.0");
}

/* ---- Test: save and load ---- */
static void test_save_load(void) {
    printf("[test_save_load]\n");
    const char *tmpfile = "/tmp/test_vectordb.vdb";

    vdb_t db;
    vdb_init(&db, 3);
    float v1[] = {1.0f, 0.0f, 0.0f};
    float v2[] = {0.0f, 1.0f, 0.0f};
    vdb_insert(&db, "saved_a", v1);
    vdb_insert(&db, "saved_b", v2);

    ASSERT(vdb_save(&db, tmpfile) == 0, "save succeeds");

    /* Load into a fresh db */
    vdb_t db2;
    ASSERT(vdb_load(&db2, tmpfile) == 0, "load succeeds");
    ASSERT(vdb_count(&db2) == 2, "loaded count is 2");
    ASSERT(db2.dim == 3, "loaded dim is 3");

    /* Search in loaded db should work */
    float q[] = {1.0f, 0.0f, 0.0f};
    const char *ids[1];
    float scores[1];
    int n = vdb_search(&db2, q, 1, ids, scores);
    ASSERT(n == 1 && strcmp(ids[0], "saved_a") == 0, "search in loaded db finds correct entry");
    ASSERT(scores[0] > 0.99f, "loaded vector matches query");

    /* Load from nonexistent file should fail */
    vdb_t db3;
    ASSERT(vdb_load(&db3, "/tmp/nonexistent_vdb_file.vdb") == -1, "load from missing file returns -1");

    remove(tmpfile);
}

int main(void) {
    printf("=== VectorDB Test Suite ===\n\n");

    test_insert_count();
    test_duplicate_insert();
    test_delete();
    test_search();
    test_search_fewer_than_k();
    test_cosine_values();
    test_save_load();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
