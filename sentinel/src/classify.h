#ifndef SENTINEL_CLASSIFY_H
#define SENTINEL_CLASSIFY_H

#include "../../src/racecar.h"

#define SENTINEL_DIM          256
#define SENTINEL_DB           "sentinel"
#define SENTINEL_TABLE_SAFE   "emails_safe"
#define SENTINEL_TABLE_SPAM   "emails_spam"
#define SENTINEL_TABLE_ATTACK "emails_attack"
#define SENTINEL_TOP_K        5
#define SENTINEL_NUM_CATEGORIES 3

typedef enum {
    EMAIL_SAFE   = 0,
    EMAIL_SPAM   = 1,
    EMAIL_ATTACK = 2,
    EMAIL_UNKNOWN = -1
} email_category_t;

typedef struct {
    email_category_t category;
    float            confidence;      /* 0.0 to 1.0 */
    float            scores[SENTINEL_NUM_CATEGORIES]; /* avg distance per category */
    int              match_counts[SENTINEL_NUM_CATEGORIES]; /* matches found per cat */
} classify_result_t;

/* Initialize sentinel database and tables. Returns RC_OK on success. */
int sentinel_init(void);

/* Check if sentinel DB exists and is populated */
int sentinel_is_initialized(void);

/* Train: add an email to a category. Combines subject+body, vectorizes, inserts. */
int sentinel_train(email_category_t cat, const char *subject, const char *body);

/* Build HNSW indexes for all 3 tables */
int sentinel_build_indexes(void);

/* Classify an email. Uses multi-process parallel search across all 3 tables.
   Fills out result struct. Returns 0 on success. */
int sentinel_classify(const char *subject, const char *body, classify_result_t *result);

/* Single-process classify (fallback, no fork) */
int sentinel_classify_single(const char *subject, const char *body, classify_result_t *result);

/* Get category name string */
const char *sentinel_category_name(email_category_t cat);

/* Get stats for each table */
int sentinel_stats(uint64_t counts[SENTINEL_NUM_CATEGORIES]);

/* Drop all sentinel data */
int sentinel_reset(void);

#endif
