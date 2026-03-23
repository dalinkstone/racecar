/*
 * tokenizer.c - Text and JSON vectorizer using feature hashing
 *
 * Converts text/JSON into fixed-dimensional float vectors using the
 * "hashing trick". No pre-trained model needed — just FNV-1a hash to
 * map tokens to dimensions, with sign hashing to reduce collision bias.
 */

#include "racecar.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

/* ================================================================
 * FNV-1a hash (32-bit)
 * ================================================================ */
static uint32_t fnv1a(const char *s, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

/* ================================================================
 * Sign hash — independent FNV-1a with different offset basis
 *
 * Returns +1 or -1.  Using a different offset gives independence
 * from the dimension hash so the sign is decorrelated.
 * ================================================================ */
static int sign_hash(const char *s, size_t len)
{
    uint32_t h = 2147483647u;   /* different offset basis */
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return (h & 1) ? 1 : -1;
}

/* ================================================================
 * Tokenizer — split text into lowercase tokens
 *
 * Returns a dynamically allocated array of token start/length pairs.
 * Tokens are stored consecutively in a lowercased copy of the input.
 * ================================================================ */
typedef struct {
    char   *buf;        /* lowercased copy (owned) */
    size_t *starts;     /* token start offsets into buf */
    size_t *lengths;    /* token lengths */
    size_t  count;      /* number of tokens */
} token_list_t;

static token_list_t tokenize(const char *text)
{
    token_list_t t = { NULL, NULL, NULL, 0 };
    if (!text) return t;

    size_t tlen = strlen(text);
    t.buf = malloc(tlen + 1);
    if (!t.buf) return t;

    /* Lowercase copy */
    for (size_t i = 0; i < tlen; i++) {
        unsigned char c = (unsigned char)text[i];
        t.buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
    }
    t.buf[tlen] = '\0';

    /* Count tokens (upper bound) */
    size_t cap = 64;
    t.starts  = malloc(cap * sizeof(size_t));
    t.lengths = malloc(cap * sizeof(size_t));
    if (!t.starts || !t.lengths) {
        free(t.buf); free(t.starts); free(t.lengths);
        t.buf = NULL; t.starts = NULL; t.lengths = NULL;
        return t;
    }

    size_t i = 0;
    while (i < tlen) {
        unsigned char c = (unsigned char)t.buf[i];

        /* Skip non-token characters: split on non-alnum ASCII,
         * but allow high bytes (>= 0x80) as part of tokens for UTF-8 */
        if (c < 0x80 && !isalnum(c)) {
            i++;
            continue;
        }

        /* Start of token */
        size_t start = i;
        while (i < tlen) {
            unsigned char cc = (unsigned char)t.buf[i];
            if (cc < 0x80 && !isalnum(cc))
                break;
            i++;
        }

        size_t length = i - start;
        if (length == 0) continue;

        /* Grow if needed */
        if (t.count >= cap) {
            cap *= 2;
            size_t *ns = realloc(t.starts,  cap * sizeof(size_t));
            size_t *nl = realloc(t.lengths, cap * sizeof(size_t));
            if (!ns || !nl) {
                if (ns) t.starts  = ns;
                if (nl) t.lengths = nl;
                break;  /* partial result is fine */
            }
            t.starts  = ns;
            t.lengths = nl;
        }

        t.starts[t.count]  = start;
        t.lengths[t.count] = length;
        t.count++;
    }

    return t;
}

static void token_list_free(token_list_t *t)
{
    free(t->buf);
    free(t->starts);
    free(t->lengths);
    t->buf     = NULL;
    t->starts  = NULL;
    t->lengths = NULL;
    t->count   = 0;
}

/* ================================================================
 * rc_vectorize_text
 * ================================================================ */
rc_status_t rc_vectorize_text(const char *text, float *out_vec, uint32_t dim)
{
    if (!text || !out_vec || dim == 0)
        return RC_ERR_INVALID;

    /* Zero the output vector */
    memset(out_vec, 0, dim * sizeof(float));

    token_list_t tokens = tokenize(text);
    if (tokens.count == 0) {
        token_list_free(&tokens);
        return RC_OK;  /* empty text -> zero vector */
    }

    /* Accumulate unigrams */
    for (size_t i = 0; i < tokens.count; i++) {
        const char *tok = tokens.buf + tokens.starts[i];
        size_t      len = tokens.lengths[i];

        uint32_t idx = fnv1a(tok, len) % dim;
        int      sgn = sign_hash(tok, len);
        out_vec[idx] += sgn * 1.0f;
    }

    /* Accumulate bigrams */
    if (tokens.count >= 2) {
        /* Temporary buffer for bigram construction */
        size_t bigram_cap = 256;
        char  *bigram_buf = malloc(bigram_cap);
        if (!bigram_buf) {
            token_list_free(&tokens);
            return RC_ERR_MEM;
        }

        for (size_t i = 0; i + 1 < tokens.count; i++) {
            size_t len1 = tokens.lengths[i];
            size_t len2 = tokens.lengths[i + 1];
            size_t need = len1 + 1 + len2; /* word1_word2 */

            if (need >= bigram_cap) {
                bigram_cap = need + 1;
                char *tmp = realloc(bigram_buf, bigram_cap);
                if (!tmp) continue;  /* skip this bigram on alloc failure */
                bigram_buf = tmp;
            }

            memcpy(bigram_buf, tokens.buf + tokens.starts[i], len1);
            bigram_buf[len1] = '_';
            memcpy(bigram_buf + len1 + 1, tokens.buf + tokens.starts[i + 1], len2);

            uint32_t idx = fnv1a(bigram_buf, need) % dim;
            int      sgn = sign_hash(bigram_buf, need);
            out_vec[idx] += sgn * 0.7f;
        }

        free(bigram_buf);
    }

    /* L2 normalize */
    float norm_sq = 0.0f;
    for (uint32_t i = 0; i < dim; i++)
        norm_sq += out_vec[i] * out_vec[i];

    if (norm_sq > 0.0f) {
        float inv_norm = 1.0f / sqrtf(norm_sq);
        for (uint32_t i = 0; i < dim; i++)
            out_vec[i] *= inv_norm;
    }

    token_list_free(&tokens);
    return RC_OK;
}

/* ================================================================
 * JSON text extraction helper
 * ================================================================ */
static void append_to_buf(char *buf, size_t *pos, size_t bufsize,
                          const char *text, size_t len)
{
    if (*pos + 1 >= bufsize) return;

    /* Add a space separator if buffer is non-empty */
    if (*pos > 0 && *pos < bufsize) {
        buf[*pos] = ' ';
        (*pos)++;
    }

    size_t avail = bufsize - *pos - 1; /* reserve 1 for null */
    size_t copy  = (len < avail) ? len : avail;
    memcpy(buf + *pos, text, copy);
    *pos += copy;
    buf[*pos] = '\0';
}

static void extract_text(rc_json_t *v, char *buf, size_t *pos, size_t bufsize)
{
    if (!v) return;

    switch (v->type) {
        case RC_JSON_STRING:
            append_to_buf(buf, pos, bufsize, v->v.string.str, v->v.string.len);
            break;

        case RC_JSON_OBJECT:
            for (size_t i = 0; i < v->v.object.count; i++) {
                /* Append "key:" + key name to capture structure */
                char key_prefixed[512];
                int n = snprintf(key_prefixed, sizeof(key_prefixed),
                                 "key:%s", v->v.object.keys[i]);
                if (n > 0)
                    append_to_buf(buf, pos, bufsize, key_prefixed, (size_t)n);

                /* Recurse into value */
                extract_text(v->v.object.vals[i], buf, pos, bufsize);
            }
            break;

        case RC_JSON_ARRAY:
            for (size_t i = 0; i < v->v.array.count; i++)
                extract_text(v->v.array.items[i], buf, pos, bufsize);
            break;

        case RC_JSON_NUMBER: {
            char num_buf[64];
            int n = snprintf(num_buf, sizeof(num_buf), "%.6g", v->v.number);
            if (n > 0)
                append_to_buf(buf, pos, bufsize, num_buf, (size_t)n);
            break;
        }

        case RC_JSON_BOOL:
            if (v->v.boolean)
                append_to_buf(buf, pos, bufsize, "true", 4);
            else
                append_to_buf(buf, pos, bufsize, "false", 5);
            break;

        case RC_JSON_NULL:
            break;
    }
}

/* ================================================================
 * rc_vectorize_json
 * ================================================================ */
rc_status_t rc_vectorize_json(const char *json_str, float *out_vec, uint32_t dim)
{
    if (!json_str || !out_vec || dim == 0)
        return RC_ERR_INVALID;

    rc_json_t *root = rc_json_parse(json_str);
    if (!root)
        return RC_ERR_INVALID;

    /* Extract all text from JSON into a buffer */
    size_t bufsize = 16384;
    char  *buf     = malloc(bufsize);
    if (!buf) {
        rc_json_free(root);
        return RC_ERR_MEM;
    }
    buf[0] = '\0';

    size_t pos = 0;
    extract_text(root, buf, &pos, bufsize);
    rc_json_free(root);

    /* Vectorize the combined text */
    rc_status_t status = rc_vectorize_text(buf, out_vec, dim);
    free(buf);
    return status;
}
