/*
 * classify.c - Sentinel email anomaly classification engine
 *
 * Uses the Racecar vector database to classify emails into
 * SAFE, SPAM, or ATTACK categories via nearest-neighbor search
 * across three dedicated tables. The multi-process classify path
 * forks three children for parallel search.
 */

#include "classify.h"
#include "../../src/racecar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>


/* ----------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------- */

static const char *TABLE_NAMES[SENTINEL_NUM_CATEGORIES] = {
    SENTINEL_TABLE_SAFE,
    SENTINEL_TABLE_SPAM,
    SENTINEL_TABLE_ATTACK
};

/*
 * Combine subject and body into a single string for vectorization.
 */
static void combine_email(const char *subject, const char *body,
                          char *buf, size_t bufsize)
{
    if (!subject) subject = "";
    if (!body)    body    = "";
    snprintf(buf, bufsize, "subject: %s body: %s", subject, body);
}

/*
 * Escape double-quotes in a source string so it is safe for JSON embedding.
 * Writes at most dst_size-1 characters and always null-terminates.
 */
static void escape_json_string(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    if (!src || dst_size == 0) {
        if (dst_size > 0) dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_size; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\';
            dst[j++] = src[i];
        } else if (src[i] == '\n') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (src[i] == '\r') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\';
            dst[j++] = 'r';
        } else if (src[i] == '\t') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\';
            dst[j++] = 't';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

/*
 * Score the results: given parallel arrays of avg distances and match counts,
 * pick the best category and compute a confidence value.
 */
static void compute_classification(const float scores[SENTINEL_NUM_CATEGORIES],
                                   const int   counts[SENTINEL_NUM_CATEGORIES],
                                   classify_result_t *result)
{
    /* Find the category with the lowest average distance */
    int best = 0;
    for (int i = 1; i < SENTINEL_NUM_CATEGORIES; i++) {
        if (scores[i] < scores[best])
            best = i;
    }

    /* Confidence: 1 - (best / second_best).
     * Sort the three scores ascending and compare the first two. */
    float sorted[SENTINEL_NUM_CATEGORIES];
    memcpy(sorted, scores, sizeof(float) * SENTINEL_NUM_CATEGORIES);
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES - 1; i++) {
        for (int j = i + 1; j < SENTINEL_NUM_CATEGORIES; j++) {
            if (sorted[j] < sorted[i]) {
                float tmp  = sorted[i];
                sorted[i]  = sorted[j];
                sorted[j]  = tmp;
            }
        }
    }

    float confidence = 0.0f;
    if (sorted[1] > 0.0001f)
        confidence = 1.0f - (sorted[0] / sorted[1]);
    if (confidence < 0.0f) confidence = 0.0f;
    if (confidence > 1.0f) confidence = 1.0f;

    /* Check if any category had results at all */
    int total_matches = 0;
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++)
        total_matches += counts[i];

    result->category = (total_matches > 0) ? (email_category_t)best : EMAIL_UNKNOWN;
    result->confidence = confidence;
    memcpy(result->scores, scores, sizeof(float) * SENTINEL_NUM_CATEGORIES);
    memcpy(result->match_counts, counts, sizeof(int) * SENTINEL_NUM_CATEGORIES);
}

/*
 * Search a single category table for the query vector.
 * Returns avg_dist via pointer, and the count of matches found.
 */
static void search_category(int cat_index, const float *vec,
                            float *avg_dist_out, int *count_out)
{
    *avg_dist_out = 1e30f;
    *count_out    = 0;

    rc_table_t *t = rc_table_open(SENTINEL_DB, TABLE_NAMES[cat_index]);
    if (!t)
        return;

    rc_result_t results[SENTINEL_TOP_K];
    uint32_t    result_count = 0;

    /* Try HNSW index first */
    char idx_path[RC_MAX_PATH];
    snprintf(idx_path, sizeof(idx_path), "%s/%s/%s.rcx",
             rc_data_dir(), SENTINEL_DB, TABLE_NAMES[cat_index]);

    rc_hnsw_t *hnsw = rc_hnsw_load(idx_path);
    if (hnsw) {
        rc_hnsw_search(hnsw, vec, SENTINEL_TOP_K,
                       RC_DEFAULT_EF_SEARCH, results, &result_count);
        rc_hnsw_free(hnsw);
    } else {
        /* Fallback to brute-force scan */
        rc_table_scan(t, vec, SENTINEL_TOP_K, results, &result_count);
    }

    if (result_count > 0) {
        float sum = 0.0f;
        for (uint32_t j = 0; j < result_count; j++)
            sum += results[j].distance;
        *avg_dist_out = sum / (float)result_count;
        *count_out    = (int)result_count;
    }

    rc_table_close(t);
}

/* ----------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------- */

int sentinel_init(void)
{
    /* Create the database (ignore if it already exists) */
    rc_status_t s = rc_db_create(SENTINEL_DB);
    if (s != RC_OK && s != RC_ERR_EXISTS) {
        fprintf(stderr, "sentinel_init: failed to create db '%s': %s\n",
                SENTINEL_DB, rc_status_str(s));
        return (int)s;
    }

    /* Create the three category tables */
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        rc_table_t *t = rc_table_create(SENTINEL_DB, TABLE_NAMES[i],
                                        SENTINEL_DIM, RC_METRIC_COSINE,
                                        RC_DEFAULT_META_SIZE);
        if (t) {
            /* Freshly created -- close it */
            rc_table_close(t);
        }
        /* If NULL, the table likely already exists -- that is fine */
    }

    return 0;
}

int sentinel_is_initialized(void)
{
    return (rc_db_exists(SENTINEL_DB) == RC_OK) ? 1 : 0;
}

int sentinel_train(email_category_t cat, const char *subject, const char *body)
{
    if (cat < 0 || cat >= SENTINEL_NUM_CATEGORIES) {
        fprintf(stderr, "sentinel_train: invalid category %d\n", (int)cat);
        return -1;
    }

    /* 1. Combine subject + body */
    char combined[8192];
    combine_email(subject, body, combined, sizeof(combined));

    /* 2. Vectorize */
    float *vec = (float *)malloc(sizeof(float) * SENTINEL_DIM);
    if (!vec) {
        fprintf(stderr, "sentinel_train: out of memory\n");
        return -1;
    }

    rc_status_t s = rc_vectorize_text(combined, vec, SENTINEL_DIM);
    if (s != RC_OK) {
        fprintf(stderr, "sentinel_train: vectorize failed: %s\n",
                rc_status_str(s));
        free(vec);
        return (int)s;
    }

    /* 3. Build metadata JSON with escaped subject */
    char escaped_subject[2048];
    escape_json_string(subject ? subject : "", escaped_subject,
                       sizeof(escaped_subject));

    char meta[RC_DEFAULT_META_SIZE];
    snprintf(meta, sizeof(meta), "{\"subject\":\"%s\"}", escaped_subject);

    /* 4. Open table, insert, close */
    rc_table_t *t = rc_table_open(SENTINEL_DB, TABLE_NAMES[cat]);
    if (!t) {
        fprintf(stderr, "sentinel_train: cannot open table '%s'\n",
                TABLE_NAMES[cat]);
        free(vec);
        return -1;
    }

    uint64_t id_out = 0;
    s = rc_table_insert(t, vec, meta, &id_out);
    if (s != RC_OK) {
        fprintf(stderr, "sentinel_train: insert failed: %s\n",
                rc_status_str(s));
        rc_table_close(t);
        free(vec);
        return (int)s;
    }

    rc_table_close(t);
    free(vec);
    return 0;
}

int sentinel_build_indexes(void)
{
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        rc_table_t *t = rc_table_open(SENTINEL_DB, TABLE_NAMES[i]);
        if (!t) {
            fprintf(stderr, "sentinel_build_indexes: cannot open table '%s'\n",
                    TABLE_NAMES[i]);
            return -1;
        }

        /* Build index path: same directory, .rcx extension */
        char idx_path[RC_MAX_PATH];
        snprintf(idx_path, sizeof(idx_path), "%s/%s/%s.rcx",
                 rc_data_dir(), SENTINEL_DB, TABLE_NAMES[i]);

        rc_status_t s = rc_hnsw_build_from_table(t, RC_DEFAULT_M,
                                                  RC_DEFAULT_EF_CONSTRUCT,
                                                  idx_path);
        rc_table_close(t);

        if (s != RC_OK) {
            fprintf(stderr, "sentinel_build_indexes: build failed for '%s': %s\n",
                    TABLE_NAMES[i], rc_status_str(s));
            return (int)s;
        }
    }

    return 0;
}

int sentinel_classify(const char *subject, const char *body,
                      classify_result_t *result)
{
    if (!result)
        return -1;

    /* 1. Vectorize the email */
    char combined[8192];
    combine_email(subject, body, combined, sizeof(combined));

    float vec[SENTINEL_DIM];
    rc_status_t s = rc_vectorize_text(combined, vec, SENTINEL_DIM);
    if (s != RC_OK) {
        fprintf(stderr, "sentinel_classify: vectorize failed: %s\n",
                rc_status_str(s));
        return (int)s;
    }

    /* 2. Create pipes (one per category) */
    int pipes[SENTINEL_NUM_CATEGORIES][2];
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        if (pipe(pipes[i]) < 0) {
            /* Close any pipes we already opened */
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return sentinel_classify_single(subject, body, result);
        }
    }

    /* 3. Fork one child per category */
    pid_t pids[SENTINEL_NUM_CATEGORIES];
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            /* Fork failed -- clean up and fall back */
            for (int j = 0; j <= i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            /* Wait for any children already forked */
            for (int j = 0; j < i; j++)
                waitpid(pids[j], NULL, 0);
            return sentinel_classify_single(subject, body, result);
        }

        if (pids[i] == 0) {
            /* ---- CHILD PROCESS ---- */
            close(pipes[i][0]); /* close read end */

            /* Close pipe ends belonging to other categories */
            for (int j = 0; j < SENTINEL_NUM_CATEGORIES; j++) {
                if (j != i) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }

            float avg_dist = 1e30f;
            int   count    = 0;
            search_category(i, vec, &avg_dist, &count);

            /* Send results back through pipe */
            write(pipes[i][1], &avg_dist, sizeof(float));
            write(pipes[i][1], &count, sizeof(int));
            close(pipes[i][1]);
            _exit(0);
        }
        /* Parent continues to fork next child */
    }

    /* 4. PARENT: close write ends, read results */
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        close(pipes[i][1]);
    }

    float scores[SENTINEL_NUM_CATEGORIES];
    int   counts[SENTINEL_NUM_CATEGORIES];

    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        ssize_t r1 = read(pipes[i][0], &scores[i], sizeof(float));
        ssize_t r2 = read(pipes[i][0], &counts[i], sizeof(int));
        close(pipes[i][0]);

        if (r1 != sizeof(float) || r2 != sizeof(int)) {
            /* Child may have crashed -- use default values */
            scores[i] = 1e30f;
            counts[i] = 0;
        }
    }

    /* 5. Wait for all children */
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        waitpid(pids[i], NULL, 0);
    }

    /* 6. Determine classification */
    compute_classification(scores, counts, result);

    return 0;
}

int sentinel_classify_single(const char *subject, const char *body,
                             classify_result_t *result)
{
    if (!result)
        return -1;

    /* 1. Vectorize the email */
    char combined[8192];
    combine_email(subject, body, combined, sizeof(combined));

    float vec[SENTINEL_DIM];
    rc_status_t s = rc_vectorize_text(combined, vec, SENTINEL_DIM);
    if (s != RC_OK) {
        fprintf(stderr, "sentinel_classify_single: vectorize failed: %s\n",
                rc_status_str(s));
        return (int)s;
    }

    /* 2. Search each category sequentially */
    float scores[SENTINEL_NUM_CATEGORIES];
    int   counts[SENTINEL_NUM_CATEGORIES];

    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        search_category(i, vec, &scores[i], &counts[i]);
    }

    /* 3. Determine classification */
    compute_classification(scores, counts, result);

    return 0;
}

const char *sentinel_category_name(email_category_t cat)
{
    switch (cat) {
        case EMAIL_SAFE:   return "SAFE";
        case EMAIL_SPAM:   return "SPAM";
        case EMAIL_ATTACK: return "ATTACK";
        default:           return "UNKNOWN";
    }
}

int sentinel_stats(uint64_t counts[SENTINEL_NUM_CATEGORIES])
{
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        counts[i] = 0;

        rc_table_t *t = rc_table_open(SENTINEL_DB, TABLE_NAMES[i]);
        if (t) {
            counts[i] = t->header.record_count;
            rc_table_close(t);
        }
    }

    return 0;
}

int sentinel_reset(void)
{
    rc_status_t s = rc_db_drop(SENTINEL_DB);
    if (s != RC_OK && s != RC_ERR_NOT_FOUND) {
        fprintf(stderr, "sentinel_reset: failed to drop db: %s\n",
                rc_status_str(s));
        return (int)s;
    }
    return 0;
}
