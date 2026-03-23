/*
 * sentinel/src/main.c - Sentinel Email Anomaly Detection CLI
 *
 * Classifies emails as SAFE, SPAM, or ATTACK using vector similarity
 * across 3 parallel vector databases powered by the Racecar engine.
 */

#include "classify.h"
#include "data.h"
#include "../../src/racecar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ================================================================
 * Helpers
 * ================================================================ */

static int is_tty(void) {
    return isatty(STDOUT_FILENO);
}

static const char *color_for_category(email_category_t cat) {
    if (!is_tty()) return "";
    switch (cat) {
        case EMAIL_SAFE:   return "\033[32m";  /* green  */
        case EMAIL_SPAM:   return "\033[33m";  /* yellow */
        case EMAIL_ATTACK: return "\033[31m";  /* red    */
        default:           return "";
    }
}

static const char *color_reset(void) {
    return is_tty() ? "\033[0m" : "";
}

static email_category_t parse_category(const char *str) {
    if (strcmp(str, "safe") == 0)   return EMAIL_SAFE;
    if (strcmp(str, "spam") == 0)   return EMAIL_SPAM;
    if (strcmp(str, "attack") == 0) return EMAIL_ATTACK;
    return EMAIL_UNKNOWN;
}

static void print_help(void) {
    printf(
        "Usage: sentinel <command> [arguments...]\n"
        "\n"
        "Commands:\n"
        "  init                                     Initialize database with sample training data\n"
        "  train <safe|spam|attack> <subject> <body> Train with a single email\n"
        "  train-file <safe|spam|attack> <file>     Train from file (one email per line: subject\\tbody)\n"
        "  classify <subject> <body>                Classify an email\n"
        "  classify-file <file>                     Classify emails from file (subject\\tbody per line)\n"
        "  classify-stdin                           Read email from stdin and classify\n"
        "  build-index                              Build HNSW indexes for fast search\n"
        "  stats                                    Show training data statistics\n"
        "  test                                     Run classification test on held-out samples\n"
        "  reset                                    Delete all sentinel data\n"
        "  help                                     Show this help\n"
    );
}

static void print_classify_result(const char *subject, const classify_result_t *r) {
    const char *cat_name = sentinel_category_name(r->category);
    const char *col = color_for_category(r->category);
    const char *rst = color_reset();

    printf("Classification: %s%s%s\n", col, cat_name, rst);
    printf("Confidence:     %.1f%%\n", r->confidence * 100.0f);
    printf("\nCategory Scores:\n");

    static const char *labels[] = {"SAFE", "SPAM", "ATTACK"};
    for (int i = 0; i < SENTINEL_NUM_CATEGORIES; i++) {
        const char *arrow = (i == (int)r->category) ? "  <-- best match" : "";
        printf("  %-7s  %.4f (%d matches)%s\n",
               labels[i], r->scores[i], r->match_counts[i], arrow);
    }

    (void)subject;  /* available for future use */
}

/* ================================================================
 * Command: init
 * ================================================================ */
static int cmd_init(void) {
    uint64_t t0 = rc_time_us();
    int rc;

    rc = sentinel_init();
    if (rc != 0) {
        fprintf(stderr, "Error: failed to initialize sentinel database\n");
        return 1;
    }

    /* Train SAFE emails */
    printf("Training SAFE emails...");
    fflush(stdout);
    for (int i = 0; i < SAFE_EMAIL_COUNT; i++) {
        rc = sentinel_train(EMAIL_SAFE, SAFE_EMAILS[i].subject, SAFE_EMAILS[i].body);
        if (rc != 0) {
            fprintf(stderr, "\nError: failed to train SAFE email %d\n", i);
            return 1;
        }
    }
    printf(" %d loaded\n", SAFE_EMAIL_COUNT);

    /* Train SPAM emails */
    printf("Training SPAM emails...");
    fflush(stdout);
    for (int i = 0; i < SPAM_EMAIL_COUNT; i++) {
        rc = sentinel_train(EMAIL_SPAM, SPAM_EMAILS[i].subject, SPAM_EMAILS[i].body);
        if (rc != 0) {
            fprintf(stderr, "\nError: failed to train SPAM email %d\n", i);
            return 1;
        }
    }
    printf(" %d loaded\n", SPAM_EMAIL_COUNT);

    /* Train ATTACK emails */
    printf("Training ATTACK emails...");
    fflush(stdout);
    for (int i = 0; i < ATTACK_EMAIL_COUNT; i++) {
        rc = sentinel_train(EMAIL_ATTACK, ATTACK_EMAILS[i].subject, ATTACK_EMAILS[i].body);
        if (rc != 0) {
            fprintf(stderr, "\nError: failed to train ATTACK email %d\n", i);
            return 1;
        }
    }
    printf(" %d loaded\n", ATTACK_EMAIL_COUNT);

    /* Build indexes */
    printf("Building HNSW indexes...");
    fflush(stdout);
    rc = sentinel_build_indexes();
    if (rc != 0) {
        fprintf(stderr, "\nError: failed to build indexes\n");
        return 1;
    }
    printf(" done\n");

    uint64_t elapsed = rc_time_us() - t0;
    int total = SAFE_EMAIL_COUNT + SPAM_EMAIL_COUNT + ATTACK_EMAIL_COUNT;
    printf("\nLoaded %d emails (%d safe, %d spam, %d attack) in %.1f ms\n",
           total, SAFE_EMAIL_COUNT, SPAM_EMAIL_COUNT, ATTACK_EMAIL_COUNT,
           elapsed / 1000.0);
    printf("Sentinel initialized and ready.\n");

    return 0;
}

/* ================================================================
 * Command: train
 * ================================================================ */
static int cmd_train(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: sentinel train <safe|spam|attack> <subject> <body>\n");
        return 1;
    }

    email_category_t cat = parse_category(argv[2]);
    if (cat == EMAIL_UNKNOWN) {
        fprintf(stderr, "Error: invalid category '%s' (use safe, spam, or attack)\n", argv[2]);
        return 1;
    }

    int rc = sentinel_train(cat, argv[3], argv[4]);
    if (rc != 0) {
        fprintf(stderr, "Error: failed to train email\n");
        return 1;
    }

    printf("Trained 1 email as %s\n", sentinel_category_name(cat));
    return 0;
}

/* ================================================================
 * Command: train-file
 * ================================================================ */
static int cmd_train_file(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: sentinel train-file <safe|spam|attack> <file>\n");
        return 1;
    }

    email_category_t cat = parse_category(argv[2]);
    if (cat == EMAIL_UNKNOWN) {
        fprintf(stderr, "Error: invalid category '%s' (use safe, spam, or attack)\n", argv[2]);
        return 1;
    }

    FILE *fp = fopen(argv[3], "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open file '%s'\n", argv[3]);
        return 1;
    }

    char line[8192];
    int count = 0;
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
        if (len == 0) continue;  /* skip blank lines */

        /* Split on tab */
        char *tab = strchr(line, '\t');
        if (!tab) {
            fprintf(stderr, "Warning: line %d has no tab separator, skipping\n", line_num);
            continue;
        }
        *tab = '\0';
        const char *subject = line;
        const char *body = tab + 1;

        int rc = sentinel_train(cat, subject, body);
        if (rc != 0) {
            fprintf(stderr, "Error: failed to train email at line %d\n", line_num);
            fclose(fp);
            return 1;
        }
        count++;
    }

    fclose(fp);
    printf("Trained %d emails as %s from '%s'\n", count, sentinel_category_name(cat), argv[3]);
    return 0;
}

/* ================================================================
 * Command: classify
 * ================================================================ */
static int cmd_classify(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: sentinel classify <subject> <body>\n");
        return 1;
    }

    if (!sentinel_is_initialized()) {
        fprintf(stderr, "Error: sentinel is not initialized. Run 'sentinel init' first.\n");
        return 1;
    }

    uint64_t t0 = rc_time_us();

    classify_result_t result;
    int rc = sentinel_classify(argv[2], argv[3], &result);
    if (rc != 0) {
        fprintf(stderr, "Error: classification failed\n");
        return 1;
    }

    uint64_t elapsed = rc_time_us() - t0;

    print_classify_result(argv[2], &result);
    printf("\nClassified in %.3f ms\n", elapsed / 1000.0);

    return 0;
}

/* ================================================================
 * Command: classify-file
 * ================================================================ */
static int cmd_classify_file(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: sentinel classify-file <file>\n");
        return 1;
    }

    if (!sentinel_is_initialized()) {
        fprintf(stderr, "Error: sentinel is not initialized. Run 'sentinel init' first.\n");
        return 1;
    }

    FILE *fp = fopen(argv[2], "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open file '%s'\n", argv[2]);
        return 1;
    }

    char line[8192];
    int total = 0;
    int counts[SENTINEL_NUM_CATEGORIES] = {0};
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
        if (len == 0) continue;

        char *tab = strchr(line, '\t');
        if (!tab) {
            fprintf(stderr, "Warning: line %d has no tab separator, skipping\n", line_num);
            continue;
        }
        *tab = '\0';
        const char *subject = line;
        const char *body = tab + 1;

        classify_result_t result;
        int rc = sentinel_classify(subject, body, &result);
        if (rc != 0) {
            fprintf(stderr, "Error: classification failed at line %d\n", line_num);
            fclose(fp);
            return 1;
        }

        const char *col = color_for_category(result.category);
        const char *rst = color_reset();
        printf("  %s%-6s%s  %.0f%%  %s\n",
               col, sentinel_category_name(result.category), rst,
               result.confidence * 100.0f, subject);

        counts[(int)result.category]++;
        total++;
    }

    fclose(fp);

    if (total > 0) {
        printf("\nResults: %d emails classified\n", total);
        printf("  SAFE:   %d (%.1f%%)\n", counts[EMAIL_SAFE],
               100.0 * counts[EMAIL_SAFE] / total);
        printf("  SPAM:   %d (%.1f%%)\n", counts[EMAIL_SPAM],
               100.0 * counts[EMAIL_SPAM] / total);
        printf("  ATTACK: %d (%.1f%%)\n", counts[EMAIL_ATTACK],
               100.0 * counts[EMAIL_ATTACK] / total);
    } else {
        printf("No emails to classify.\n");
    }

    return 0;
}

/* ================================================================
 * Command: classify-stdin
 * ================================================================ */
static int cmd_classify_stdin(void) {
    if (!sentinel_is_initialized()) {
        fprintf(stderr, "Error: sentinel is not initialized. Run 'sentinel init' first.\n");
        return 1;
    }

    /* Read all of stdin */
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "Error: out of memory\n");
        return 1;
    }

    size_t n;
    while ((n = fread(buf + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                fprintf(stderr, "Error: out of memory\n");
                free(buf);
                return 1;
            }
            buf = tmp;
        }
    }
    buf[len] = '\0';

    if (len == 0) {
        fprintf(stderr, "Error: no input received on stdin\n");
        free(buf);
        return 1;
    }

    /* First line is subject, rest is body */
    char *newline = strchr(buf, '\n');
    const char *subject;
    const char *body;

    if (newline) {
        *newline = '\0';
        subject = buf;
        body = newline + 1;
    } else {
        subject = buf;
        body = "";
    }

    uint64_t t0 = rc_time_us();

    classify_result_t result;
    int rc = sentinel_classify(subject, body, &result);
    if (rc != 0) {
        fprintf(stderr, "Error: classification failed\n");
        free(buf);
        return 1;
    }

    uint64_t elapsed = rc_time_us() - t0;

    print_classify_result(subject, &result);
    printf("\nClassified in %.3f ms\n", elapsed / 1000.0);

    free(buf);
    return 0;
}

/* ================================================================
 * Command: build-index
 * ================================================================ */
static int cmd_build_index(void) {
    if (!sentinel_is_initialized()) {
        fprintf(stderr, "Error: sentinel is not initialized. Run 'sentinel init' first.\n");
        return 1;
    }

    printf("Building HNSW indexes...\n");
    uint64_t t0 = rc_time_us();

    int rc = sentinel_build_indexes();
    if (rc != 0) {
        fprintf(stderr, "Error: failed to build indexes\n");
        return 1;
    }

    uint64_t elapsed = rc_time_us() - t0;
    printf("Indexes built in %.1f ms\n", elapsed / 1000.0);
    return 0;
}

/* ================================================================
 * Command: stats
 * ================================================================ */
static int cmd_stats(void) {
    if (!sentinel_is_initialized()) {
        fprintf(stderr, "Error: sentinel is not initialized. Run 'sentinel init' first.\n");
        return 1;
    }

    uint64_t counts[SENTINEL_NUM_CATEGORIES];
    int rc = sentinel_stats(counts);
    if (rc != 0) {
        fprintf(stderr, "Error: failed to retrieve stats\n");
        return 1;
    }

    uint64_t total = counts[EMAIL_SAFE] + counts[EMAIL_SPAM] + counts[EMAIL_ATTACK];
    printf("Sentinel Database Statistics:\n");
    printf("  SAFE emails:   %llu\n", (unsigned long long)counts[EMAIL_SAFE]);
    printf("  SPAM emails:   %llu\n", (unsigned long long)counts[EMAIL_SPAM]);
    printf("  ATTACK emails: %llu\n", (unsigned long long)counts[EMAIL_ATTACK]);
    printf("  Total:         %llu\n", (unsigned long long)total);

    return 0;
}

/* ================================================================
 * Command: test
 * ================================================================ */

typedef struct {
    email_category_t expected;
    const char *subject;
    const char *body;
} test_case_t;

static const test_case_t TEST_CASES[] = {
    {EMAIL_SAFE,   "Re: Project Update",
     "Thanks for the update. The new feature looks great. Let's discuss in tomorrow's standup."},
    {EMAIL_SAFE,   "Lunch tomorrow?",
     "Hey, want to grab lunch at the Thai place tomorrow around noon?"},
    {EMAIL_SAFE,   "Q3 Budget Review",
     "Please find attached the Q3 budget summary. All departments are within allocation."},
    {EMAIL_SPAM,   "AMAZING DEAL - 90% OFF!!!",
     "Limited time offer! Get premium watches at 90% discount. Buy now before stock runs out! Free shipping worldwide!"},
    {EMAIL_SPAM,   "You've won $5,000,000",
     "Congratulations! You have been selected as the winner of our international lottery. Send your bank details to claim your prize."},
    {EMAIL_SPAM,   "Lose 30 Pounds in 30 Days",
     "Revolutionary new diet pill guaranteed to help you lose weight fast. No exercise needed. Order now and get a free bottle."},
    {EMAIL_ATTACK, "Urgent: Password Reset Required",
     "Your account password will expire in 24 hours. Click here immediately to reset your password or your account will be locked."},
    {EMAIL_ATTACK, "Wire Transfer Request",
     "Hi, I need you to process an urgent wire transfer of $45,000 to the account below. This is confidential, do not discuss with anyone."},
    {EMAIL_ATTACK, "Unusual Sign-in Activity",
     "We detected a sign-in to your account from an unrecognized device. If this wasn't you, secure your account immediately by clicking below."},
    {EMAIL_ATTACK, "Invoice #INV-29481 Attached",
     "Please review the attached invoice for recent services. Open the document to verify the charges and approve payment."},
    {EMAIL_SPAM,   "Work From Home - Earn $5000/week",
     "Start earning money from home today. No experience needed. Our proven system has helped thousands achieve financial freedom."},
    {EMAIL_SAFE,   "Team Building Event - Friday",
     "Hi everyone, just a reminder about our team building event this Friday at 3pm. We'll be doing an escape room activity. Please RSVP."},
};

#define NUM_TEST_CASES ((int)(sizeof(TEST_CASES) / sizeof(TEST_CASES[0])))

static int cmd_test(void) {
    if (!sentinel_is_initialized()) {
        fprintf(stderr, "Error: sentinel is not initialized. Run 'sentinel init' first.\n");
        return 1;
    }

    int tty = is_tty();
    int correct = 0;
    int total = NUM_TEST_CASES;

    printf("Test Results:\n");

    for (int i = 0; i < total; i++) {
        const test_case_t *tc = &TEST_CASES[i];
        classify_result_t result;

        int rc = sentinel_classify(tc->subject, tc->body, &result);
        if (rc != 0) {
            fprintf(stderr, "  Error classifying test %d\n", i);
            continue;
        }

        int pass = (result.category == tc->expected);
        if (pass) correct++;

        const char *expected_name = sentinel_category_name(tc->expected);
        const char *actual_name = sentinel_category_name(result.category);
        const char *col = color_for_category(result.category);
        const char *rst = color_reset();

        if (pass) {
            const char *mark = tty ? "\xe2\x9c\x93" : "[PASS]";
            const char *green = tty ? "\033[32m" : "";
            const char *r = tty ? "\033[0m" : "";
            printf("  %s%s%s %-7s \"%s\" -> %s%s%s (correct)\n",
                   green, mark, r, expected_name, tc->subject,
                   col, actual_name, rst);
        } else {
            const char *mark = tty ? "\xe2\x9c\x97" : "[FAIL]";
            const char *red = tty ? "\033[31m" : "";
            const char *r = tty ? "\033[0m" : "";
            printf("  %s%s%s %-7s \"%s\" -> %s%s%s (WRONG, expected %s)\n",
                   red, mark, r, expected_name, tc->subject,
                   col, actual_name, rst, expected_name);
        }
    }

    printf("\nAccuracy: %d/%d (%.1f%%)\n", correct, total,
           total > 0 ? 100.0 * correct / total : 0.0);

    return 0;
}

/* ================================================================
 * Command: reset
 * ================================================================ */
static int cmd_reset(void) {
    int rc = sentinel_reset();
    if (rc != 0) {
        fprintf(stderr, "Error: failed to reset sentinel data\n");
        return 1;
    }
    printf("All sentinel data cleared.\n");
    return 0;
}

/* ================================================================
 * main
 * ================================================================ */
int main(int argc, char **argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "init") == 0)              return cmd_init();
    else if (strcmp(cmd, "train") == 0)        return cmd_train(argc, argv);
    else if (strcmp(cmd, "train-file") == 0)   return cmd_train_file(argc, argv);
    else if (strcmp(cmd, "classify") == 0)     return cmd_classify(argc, argv);
    else if (strcmp(cmd, "classify-file") == 0) return cmd_classify_file(argc, argv);
    else if (strcmp(cmd, "classify-stdin") == 0) return cmd_classify_stdin();
    else if (strcmp(cmd, "build-index") == 0)  return cmd_build_index();
    else if (strcmp(cmd, "stats") == 0)        return cmd_stats();
    else if (strcmp(cmd, "test") == 0)         return cmd_test();
    else if (strcmp(cmd, "reset") == 0)        return cmd_reset();
    else if (strcmp(cmd, "help") == 0)         { print_help(); return 0; }
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        fprintf(stderr, "Run 'sentinel help' for usage.\n");
        return 1;
    }
}
