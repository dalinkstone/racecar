/*
 * json.c - Minimal recursive-descent JSON parser for Racecar
 *
 * Pure C, no external dependencies. Handles the full JSON spec
 * minus \uXXXX surrogate pairs (ASCII-only for \u escapes).
 */

#include "racecar.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ================================================================
 * Parser state
 * ================================================================ */
typedef struct {
    const char *str;
    size_t      pos;
} json_parser_t;

/* Forward declarations */
static rc_json_t *parse_value(json_parser_t *p);

/* ================================================================
 * Helpers
 * ================================================================ */
static void skip_ws(json_parser_t *p)
{
    while (p->str[p->pos] &&
           (p->str[p->pos] == ' '  || p->str[p->pos] == '\t' ||
            p->str[p->pos] == '\n' || p->str[p->pos] == '\r'))
        p->pos++;
}

static char peek(json_parser_t *p)
{
    return p->str[p->pos];
}

static char advance(json_parser_t *p)
{
    return p->str[p->pos++];
}

static int match(json_parser_t *p, char ch)
{
    if (p->str[p->pos] == ch) {
        p->pos++;
        return 1;
    }
    return 0;
}

/* Allocate a json value node */
static rc_json_t *json_alloc(rc_json_type_t type)
{
    rc_json_t *v = calloc(1, sizeof(rc_json_t));
    if (v) v->type = type;
    return v;
}

/* ================================================================
 * parse_string
 * ================================================================ */
static rc_json_t *parse_string(json_parser_t *p)
{
    if (!match(p, '"'))
        return NULL;

    /* First pass: compute length needed */
    size_t cap = 64;
    char  *buf = malloc(cap);
    if (!buf) return NULL;
    size_t len = 0;

    while (peek(p) != '\0' && peek(p) != '"') {
        char c = advance(p);

        if (c == '\\') {
            c = advance(p);
            switch (c) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u': {
                    /* \uXXXX — parse 4 hex digits, emit ASCII if <=127,
                     * otherwise emit '?' as placeholder */
                    unsigned codepoint = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = advance(p);
                        codepoint <<= 4;
                        if (h >= '0' && h <= '9')      codepoint |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') codepoint |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') codepoint |= (unsigned)(h - 'A' + 10);
                    }
                    if (codepoint <= 127)
                        c = (char)codepoint;
                    else
                        c = '?';
                    break;
                }
                default:
                    /* Unknown escape — keep as-is */
                    break;
            }
        }

        /* Grow buffer if needed */
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
        buf[len++] = c;
    }

    if (!match(p, '"')) {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';

    rc_json_t *v = json_alloc(RC_JSON_STRING);
    if (!v) { free(buf); return NULL; }
    v->v.string.str = buf;
    v->v.string.len = len;
    return v;
}

/* ================================================================
 * parse_number
 * ================================================================ */
static rc_json_t *parse_number(json_parser_t *p)
{
    char *end = NULL;
    double num = strtod(p->str + p->pos, &end);
    if (end == p->str + p->pos)
        return NULL;

    p->pos = (size_t)(end - p->str);

    rc_json_t *v = json_alloc(RC_JSON_NUMBER);
    if (!v) return NULL;
    v->v.number = num;
    return v;
}

/* ================================================================
 * parse_object
 * ================================================================ */
static rc_json_t *parse_object(json_parser_t *p)
{
    if (!match(p, '{'))
        return NULL;

    size_t cap   = 8;
    size_t count = 0;
    char       **keys = malloc(cap * sizeof(char *));
    rc_json_t  **vals = malloc(cap * sizeof(rc_json_t *));
    if (!keys || !vals) { free(keys); free(vals); return NULL; }

    skip_ws(p);

    if (peek(p) != '}') {
        for (;;) {
            skip_ws(p);

            /* Parse key (must be a string) */
            rc_json_t *key_val = parse_string(p);
            if (!key_val) goto fail;

            char *key = key_val->v.string.str;
            key_val->v.string.str = NULL; /* steal the string */
            free(key_val);

            skip_ws(p);
            if (!match(p, ':')) { free(key); goto fail; }

            skip_ws(p);
            rc_json_t *value = parse_value(p);
            if (!value) { free(key); goto fail; }

            /* Grow arrays if needed */
            if (count >= cap) {
                cap *= 2;
                char      **tk = realloc(keys, cap * sizeof(char *));
                rc_json_t **tv = realloc(vals, cap * sizeof(rc_json_t *));
                if (!tk || !tv) {
                    if (tk) keys = tk;
                    if (tv) vals = tv;
                    free(key);
                    rc_json_free(value);
                    goto fail;
                }
                keys = tk;
                vals = tv;
            }

            keys[count] = key;
            vals[count] = value;
            count++;

            skip_ws(p);
            if (!match(p, ','))
                break;
        }
    }

    if (!match(p, '}'))
        goto fail;

    rc_json_t *v = json_alloc(RC_JSON_OBJECT);
    if (!v) goto fail;
    v->v.object.keys  = keys;
    v->v.object.vals  = vals;
    v->v.object.count = count;
    return v;

fail:
    for (size_t i = 0; i < count; i++) {
        free(keys[i]);
        rc_json_free(vals[i]);
    }
    free(keys);
    free(vals);
    return NULL;
}

/* ================================================================
 * parse_array
 * ================================================================ */
static rc_json_t *parse_array(json_parser_t *p)
{
    if (!match(p, '['))
        return NULL;

    size_t cap   = 8;
    size_t count = 0;
    rc_json_t **items = malloc(cap * sizeof(rc_json_t *));
    if (!items) return NULL;

    skip_ws(p);

    if (peek(p) != ']') {
        for (;;) {
            skip_ws(p);
            rc_json_t *item = parse_value(p);
            if (!item) goto fail;

            if (count >= cap) {
                cap *= 2;
                rc_json_t **tmp = realloc(items, cap * sizeof(rc_json_t *));
                if (!tmp) { rc_json_free(item); goto fail; }
                items = tmp;
            }

            items[count++] = item;

            skip_ws(p);
            if (!match(p, ','))
                break;
        }
    }

    if (!match(p, ']'))
        goto fail;

    rc_json_t *v = json_alloc(RC_JSON_ARRAY);
    if (!v) goto fail;
    v->v.array.items = items;
    v->v.array.count = count;
    return v;

fail:
    for (size_t i = 0; i < count; i++)
        rc_json_free(items[i]);
    free(items);
    return NULL;
}

/* ================================================================
 * parse_bool
 * ================================================================ */
static rc_json_t *parse_bool(json_parser_t *p)
{
    if (strncmp(p->str + p->pos, "true", 4) == 0) {
        p->pos += 4;
        rc_json_t *v = json_alloc(RC_JSON_BOOL);
        if (v) v->v.boolean = 1;
        return v;
    }
    if (strncmp(p->str + p->pos, "false", 5) == 0) {
        p->pos += 5;
        rc_json_t *v = json_alloc(RC_JSON_BOOL);
        if (v) v->v.boolean = 0;
        return v;
    }
    return NULL;
}

/* ================================================================
 * parse_null
 * ================================================================ */
static rc_json_t *parse_null(json_parser_t *p)
{
    if (strncmp(p->str + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return json_alloc(RC_JSON_NULL);
    }
    return NULL;
}

/* ================================================================
 * parse_value — main dispatch
 * ================================================================ */
static rc_json_t *parse_value(json_parser_t *p)
{
    skip_ws(p);
    char c = peek(p);

    if (c == '"')                          return parse_string(p);
    if (c == '{')                          return parse_object(p);
    if (c == '[')                          return parse_array(p);
    if (c == 't' || c == 'f')             return parse_bool(p);
    if (c == 'n')                          return parse_null(p);
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(p);

    return NULL;
}

/* ================================================================
 * Public API
 * ================================================================ */

rc_json_t *rc_json_parse(const char *str)
{
    if (!str) return NULL;

    json_parser_t p = { .str = str, .pos = 0 };
    rc_json_t *v = parse_value(&p);

    /* Check that remaining input is only whitespace */
    if (v) {
        skip_ws(&p);
        if (p.str[p.pos] != '\0') {
            rc_json_free(v);
            return NULL;
        }
    }

    return v;
}

void rc_json_free(rc_json_t *v)
{
    if (!v) return;

    switch (v->type) {
        case RC_JSON_STRING:
            free(v->v.string.str);
            break;

        case RC_JSON_ARRAY:
            for (size_t i = 0; i < v->v.array.count; i++)
                rc_json_free(v->v.array.items[i]);
            free(v->v.array.items);
            break;

        case RC_JSON_OBJECT:
            for (size_t i = 0; i < v->v.object.count; i++) {
                free(v->v.object.keys[i]);
                rc_json_free(v->v.object.vals[i]);
            }
            free(v->v.object.keys);
            free(v->v.object.vals);
            break;

        default:
            break;
    }

    free(v);
}

const char *rc_json_get_str(rc_json_t *obj, const char *key)
{
    if (!obj || !key || obj->type != RC_JSON_OBJECT)
        return NULL;

    for (size_t i = 0; i < obj->v.object.count; i++) {
        if (strcmp(obj->v.object.keys[i], key) == 0) {
            rc_json_t *val = obj->v.object.vals[i];
            if (val && val->type == RC_JSON_STRING)
                return val->v.string.str;
            return NULL;
        }
    }
    return NULL;
}

double rc_json_get_num(rc_json_t *obj, const char *key)
{
    if (!obj || !key || obj->type != RC_JSON_OBJECT)
        return 0.0;

    for (size_t i = 0; i < obj->v.object.count; i++) {
        if (strcmp(obj->v.object.keys[i], key) == 0) {
            rc_json_t *val = obj->v.object.vals[i];
            if (val && val->type == RC_JSON_NUMBER)
                return val->v.number;
            return 0.0;
        }
    }
    return 0.0;
}

rc_json_t *rc_json_get(rc_json_t *obj, const char *key)
{
    if (!obj || !key || obj->type != RC_JSON_OBJECT)
        return NULL;

    for (size_t i = 0; i < obj->v.object.count; i++) {
        if (strcmp(obj->v.object.keys[i], key) == 0)
            return obj->v.object.vals[i];
    }
    return NULL;
}

/* ================================================================
 * rc_json_dump — serialize JSON value to string
 * ================================================================ */

/* Helper: write escaped JSON string into buffer */
static int dump_string(const char *s, size_t slen, char *buf, size_t bufsize)
{
    int written = 0;
    int total   = 0;

    /* opening quote */
    if ((size_t)total < bufsize && buf)
        buf[total] = '"';
    total++;

    for (size_t i = 0; i < slen; i++) {
        const char *esc = NULL;
        char esc_buf[8];

        switch (s[i]) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\b': esc = "\\b";  break;
            case '\f': esc = "\\f";  break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if ((unsigned char)s[i] < 0x20) {
                    snprintf(esc_buf, sizeof(esc_buf), "\\u%04x", (unsigned char)s[i]);
                    esc = esc_buf;
                }
                break;
        }

        if (esc) {
            size_t elen = strlen(esc);
            for (size_t j = 0; j < elen; j++) {
                if ((size_t)total < bufsize && buf)
                    buf[total] = esc[j];
                total++;
            }
        } else {
            if ((size_t)total < bufsize && buf)
                buf[total] = s[i];
            total++;
        }
    }

    /* closing quote */
    if ((size_t)total < bufsize && buf)
        buf[total] = '"';
    total++;

    (void)written;
    return total;
}

int rc_json_dump(rc_json_t *v, char *buf, size_t bufsize)
{
    if (!v) return 0;

    int total = 0;

    /* Macro to safely append characters */
    #define EMIT_CHAR(c) do {                          \
        if (buf && (size_t)total < bufsize)            \
            buf[total] = (c);                          \
        total++;                                       \
    } while (0)

    #define EMIT_STR(s) do {                           \
        const char *_s = (s);                          \
        while (*_s) { EMIT_CHAR(*_s); _s++; }         \
    } while (0)

    switch (v->type) {
        case RC_JSON_NULL:
            EMIT_STR("null");
            break;

        case RC_JSON_BOOL:
            if (v->v.boolean)
                EMIT_STR("true");
            else
                EMIT_STR("false");
            break;

        case RC_JSON_NUMBER: {
            char num_buf[64];
            int n = snprintf(num_buf, sizeof(num_buf), "%.17g", v->v.number);
            for (int i = 0; i < n; i++)
                EMIT_CHAR(num_buf[i]);
            break;
        }

        case RC_JSON_STRING: {
            int n = dump_string(v->v.string.str, v->v.string.len,
                                buf ? buf + total : NULL,
                                buf ? (bufsize > (size_t)total ? bufsize - (size_t)total : 0) : 0);
            total += n;
            break;
        }

        case RC_JSON_ARRAY:
            EMIT_CHAR('[');
            for (size_t i = 0; i < v->v.array.count; i++) {
                if (i > 0) EMIT_CHAR(',');
                int n = rc_json_dump(v->v.array.items[i],
                                     buf ? buf + total : NULL,
                                     buf ? (bufsize > (size_t)total ? bufsize - (size_t)total : 0) : 0);
                total += n;
            }
            EMIT_CHAR(']');
            break;

        case RC_JSON_OBJECT:
            EMIT_CHAR('{');
            for (size_t i = 0; i < v->v.object.count; i++) {
                if (i > 0) EMIT_CHAR(',');
                /* key */
                size_t klen = strlen(v->v.object.keys[i]);
                int n = dump_string(v->v.object.keys[i], klen,
                                    buf ? buf + total : NULL,
                                    buf ? (bufsize > (size_t)total ? bufsize - (size_t)total : 0) : 0);
                total += n;
                EMIT_CHAR(':');
                /* value */
                n = rc_json_dump(v->v.object.vals[i],
                                 buf ? buf + total : NULL,
                                 buf ? (bufsize > (size_t)total ? bufsize - (size_t)total : 0) : 0);
                total += n;
            }
            EMIT_CHAR('}');
            break;
    }

    /* Null-terminate if there's room */
    if (buf && (size_t)total < bufsize)
        buf[total] = '\0';

    #undef EMIT_CHAR
    #undef EMIT_STR

    return total;
}
