#ifndef SENTINEL_DATA_H
#define SENTINEL_DATA_H

#define SENTINEL_DIM          256
#define SENTINEL_DB           "sentinel"
#define SENTINEL_TABLE_SAFE   "emails_safe"
#define SENTINEL_TABLE_SPAM   "emails_spam"
#define SENTINEL_TABLE_ATTACK "emails_attack"

typedef struct {
    const char *subject;
    const char *body;
} sample_email_t;

/* Arrays of sample emails per category */
extern const sample_email_t SAFE_EMAILS[];
extern const int SAFE_EMAIL_COUNT;

extern const sample_email_t SPAM_EMAILS[];
extern const int SPAM_EMAIL_COUNT;

extern const sample_email_t ATTACK_EMAILS[];
extern const int ATTACK_EMAIL_COUNT;

#endif
