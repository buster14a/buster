// Bounded JSON and output primitives for hosted CI observations (#2823/#2824).
// cm_json_parse uses an explicit grammar stack; duplicate keys fail closed.
// cm_quote and cm_escape own the JSON/Markdown/CSV output boundaries.
#ifndef BUSTER_CI_METRICS_JSON_H
#define BUSTER_CI_METRICS_JSON_H
#include <buster/lib/base.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#define CM_BYTES (16u * 1024u * 1024u)
#define CM_TOKENS 131072u
#define CM_DEPTH 64u
#define CM_FIELD 2048u
#define CM_ROWS 32768u
#define CM_BRANCH "ci-timing-history"
#define CM_REPO "buster14a/buster"
#define CM_SCHEMA "buster-hosted-ci-execution-v1"
#define CM_POLICY "hosted-ci-cohort-v1"
#define CM_REPORTER "a36422384d0334a53d4be73bc306b97ccdba4768"
typedef struct CmToken CmToken;
struct CmToken { char *text; unsigned child, next; char kind; };
typedef struct CmJson CmJson;
struct CmJson { CmToken *tokens; char *arena; unsigned count, used, capacity; int valid; };
typedef struct CmFrame CmFrame;
struct CmFrame { unsigned node, tail; int state; };
BUSTER_GLOBAL_LOCAL int cm_equal(const char *a, const char *b)
{
    int result = a && b && strcmp(a, b) == 0;
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_hex(char c)
{
    int result = c >= '0' && c <= '9' ? c - '0' :
        c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_sha(const char *s)
{
    int result = s && strlen(s) == 40;
    for (unsigned i = 0; result && i < 40; ++i) result = cm_hex(s[i]) >= 0 && !(s[i] >= 'A' && s[i] <= 'F');
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_unsigned(const char *text, uint64_t *out)
{
    int valid = text && text[0];
    uint64_t value = 0;
    for (size_t i = 0; valid && text[i]; ++i)
    {
        unsigned digit = (unsigned)(text[i] - '0');
        valid = digit <= 9 && value <= (UINT64_MAX - digit) / 10;
        if (valid) value = value * 10 + digit;
    }
    if (valid) *out = value;
    return valid;
}
BUSTER_GLOBAL_LOCAL void cm_copy(char *out, size_t capacity, const char *text)
{
    if (capacity) snprintf(out, capacity, "%s", text ? text : "");
}
BUSTER_GLOBAL_LOCAL uint64_t cm_hash(const char *s)
{
    uint64_t value = UINT64_C(14695981039346656037);
    for (size_t i = 0; s && s[i]; ++i) { value ^= (unsigned char)s[i]; value *= UINT64_C(1099511628211); }
    return value;
}
BUSTER_GLOBAL_LOCAL int cm_utf8(const unsigned char *s, size_t n)
{
    int valid = 1;
    size_t i = 0;
    while (valid && i < n)
    {
        unsigned c = s[i++], cp = c, extra = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; extra = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; extra = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; extra = 3; minimum = 0x10000; }
        else if (c >= 0x80) valid = 0;
        valid = valid && extra <= n - i;
        for (unsigned k = 0; valid && k < extra; ++k)
        {
            unsigned part = s[i++]; valid = (part & 0xc0) == 0x80;
            cp = (cp << 6) | (part & 63);
        }
        valid = valid && (!extra || cp >= minimum) && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL char *cm_string(CmJson *j, const char *input, size_t n, size_t *cursor)
{
    unsigned start = j->used;
    int valid = *cursor < n && input[(*cursor)++] == '"', closed = 0;
    while (valid && *cursor < n && !closed)
    {
        unsigned c = (unsigned char)input[(*cursor)++];
        if (c == '"') closed = 1;
        else
        {
            valid = c >= 32 && j->used + 5 < j->capacity;
            if (valid && c == '\\')
            {
                valid = *cursor < n; c = valid ? (unsigned char)input[(*cursor)++] : 0;
                if (c == 'u')
                {
                    unsigned cp = 0;
                    for (unsigned k = 0; valid && k < 4; ++k)
                    {
                        int digit = *cursor < n ? cm_hex(input[(*cursor)++]) : -1;
                        valid = digit >= 0; cp = cp * 16 + (unsigned)(digit >= 0 ? digit : 0);
                    }
                    if (valid && cp >= 0xd800 && cp <= 0xdbff)
                    {
                        valid = n - *cursor >= 6 && input[*cursor] == '\\' && input[*cursor + 1] == 'u';
                        *cursor += valid ? 2 : 0;
                        unsigned low = 0;
                        for (unsigned k = 0; valid && k < 4; ++k)
                        {
                            int digit = cm_hex(input[(*cursor)++]);
                            valid = digit >= 0; low = low * 16 + (unsigned)(digit >= 0 ? digit : 0);
                        }
                        valid = valid && low >= 0xdc00 && low <= 0xdfff;
                        cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                    }
                    valid = valid && cp && !(cp >= 0xd800 && cp <= 0xdfff);
                    if (valid)
                    {
                        if (cp < 128) j->arena[j->used++] = (char)cp;
                        else
                        {
                            unsigned count = cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
                            j->arena[j->used++] = (char)((count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0) | (cp >> (6 * (count - 1))));
                            for (unsigned k = count - 1; k; --k) j->arena[j->used++] = (char)(0x80 | ((cp >> (6 * (k - 1))) & 63));
                        }
                    }
                    c = 0;
                }
                else if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
                else if (c == 'b') c = '\b';
                else if (c == 'f') c = '\f';
                else valid = c == '"' || c == '\\' || c == '/';
            }
            if (valid && c) j->arena[j->used++] = (char)c;
        }
    }
    valid = valid && closed && j->used < j->capacity;
    char *result = NULL;
    if (valid)
    {
        j->arena[j->used++] = 0; result = j->arena + start;
        valid = cm_utf8((unsigned char *)result, strlen(result));
    }
    j->valid &= valid;
    return result;
}
BUSTER_GLOBAL_LOCAL void cm_json_free(CmJson *j)
{
    free(j->tokens); free(j->arena); memset(j, 0, sizeof(*j));
}
BUSTER_GLOBAL_LOCAL CmJson cm_json_parse(const char *input, size_t n)
{
    CmJson j = {0};
    j.capacity = n <= CM_BYTES ? (unsigned)n + 8 : 0;
    j.arena = j.capacity ? malloc(j.capacity) : NULL;
    j.tokens = j.capacity ? calloc(CM_TOKENS, sizeof(*j.tokens)) : NULL;
    j.valid = j.arena && j.tokens && n > 0 && n <= CM_BYTES;
    CmFrame stack[CM_DEPTH];
    unsigned depth = 0;
    size_t p = 0;
    int root_done = 0;
    while (j.valid && p < n)
    {
        while (p < n && (input[p] == ' ' || input[p] == '\n' || input[p] == '\r' || input[p] == '\t')) ++p;
        if (p < n)
        {
            CmFrame *f = depth ? &stack[depth - 1] : NULL;
            if (f && (f->state == 1 || f->state == 3))
            {
                char expected = f->state == 1 ? ':' : ',';
                char close = j.tokens[f->node].kind == 'o' ? '}' : ']';
                if (f->state == 3 && input[p] == close) { ++p; --depth; root_done = depth == 0; }
                else { j.valid = input[p++] == expected; f->state = expected == ':' ? 2 : 4; }
            }
            else if (f && input[p] == (j.tokens[f->node].kind == 'o' ? '}' : ']'))
            {
                j.valid = f->state == 0; ++p; --depth; root_done = depth == 0;
            }
            else
            {
                j.valid = !root_done && j.count + 1 < CM_TOKENS;
                unsigned node = ++j.count;
                if (j.valid)
                {
                    CmToken *t = &j.tokens[node];
                    int key = f && j.tokens[f->node].kind == 'o' && (f->state == 0 || f->state == 4);
                    if (f)
                    {
                        if (f->tail) j.tokens[f->tail].next = node;
                        else j.tokens[f->node].child = node;
                        f->tail = node; f->state = key ? 1 : 3;
                    }
                    char c = input[p];
                    if (c == '"') { t->kind = 's'; t->text = cm_string(&j, input, n, &p); }
                    else if ((c == '{' || c == '[') && !key)
                    {
                        t->kind = c == '{' ? 'o' : 'a'; ++p; j.valid = depth < CM_DEPTH;
                        if (j.valid) stack[depth++] = (CmFrame){node, 0, 0};
                    }
                    else if (!key)
                    {
                        size_t start = p;
                        while (p < n && input[p] != ',' && input[p] != '}' && input[p] != ']' &&
                               input[p] != ' ' && input[p] != '\n' && input[p] != '\r' && input[p] != '\t') ++p;
                        size_t length = p - start;
                        j.valid = length && j.used + length + 1 <= j.capacity;
                        if (j.valid)
                        {
                            t->text = j.arena + j.used; memcpy(t->text, input + start, length);
                            j.used += (unsigned)length; j.arena[j.used++] = 0; t->kind = 'v';
                            if (!cm_equal(t->text, "null") && !cm_equal(t->text, "true") && !cm_equal(t->text, "false"))
                            {
                                size_t q = 0;
                                if (t->text[q] == '-') ++q;
                                if (t->text[q] == '0') ++q;
                                else
                                {
                                    j.valid = t->text[q] >= '1' && t->text[q] <= '9';
                                    while (t->text[q] >= '0' && t->text[q] <= '9') ++q;
                                }
                                if (t->text[q] == '.')
                                {
                                    ++q; j.valid &= t->text[q] >= '0' && t->text[q] <= '9';
                                    while (t->text[q] >= '0' && t->text[q] <= '9') ++q;
                                }
                                if (t->text[q] == 'e' || t->text[q] == 'E')
                                {
                                    ++q; if (t->text[q] == '+' || t->text[q] == '-') ++q;
                                    j.valid &= t->text[q] >= '0' && t->text[q] <= '9';
                                    while (t->text[q] >= '0' && t->text[q] <= '9') ++q;
                                }
                                j.valid &= t->text[q] == 0;
                            }
                        }
                    }
                    else j.valid = 0;
                    if (!depth) root_done = 1;
                }
            }
        }
    }
    j.valid &= root_done && !depth;
    for (unsigned i = 1; j.valid && i <= j.count; ++i)
    {
        if (j.tokens[i].kind == 'o')
        {
            unsigned keys = 0;
            for (unsigned a = j.tokens[i].child; j.valid && a; )
            {
                unsigned value = j.tokens[a].next;
                j.valid = j.tokens[a].kind == 's' && value && ++keys <= 256;
                for (unsigned b = value ? j.tokens[value].next : 0; j.valid && b; )
                {
                    j.valid = !cm_equal(j.tokens[a].text, j.tokens[b].text);
                    unsigned v = j.tokens[b].next; b = v ? j.tokens[v].next : 0;
                }
                a = value ? j.tokens[value].next : 0;
            }
        }
    }
    return j;
}
BUSTER_GLOBAL_LOCAL unsigned cm_member(const CmJson *j, unsigned object, const char *key)
{
    unsigned result = 0;
    if (j->valid && object && object <= j->count && j->tokens[object].kind == 'o')
    {
        for (unsigned k = j->tokens[object].child; k; )
        {
            unsigned value = j->tokens[k].next;
            if (cm_equal(j->tokens[k].text, key)) result = value;
            k = value ? j->tokens[value].next : 0;
        }
    }
    return result;
}
BUSTER_GLOBAL_LOCAL const char *cm_value(const CmJson *j, unsigned node)
{
    const char *result = "";
    if (j->valid && node && node <= j->count && j->tokens[node].text) result = j->tokens[node].text;
    return result;
}
BUSTER_GLOBAL_LOCAL const char *cm_get(const CmJson *j, unsigned node, const char *key)
{
    const char *result = cm_value(j, cm_member(j, node, key));
    return result;
}
BUSTER_GLOBAL_LOCAL uint64_t cm_number(const CmJson *j, unsigned node, const char *key)
{
    uint64_t result = 0; cm_unsigned(cm_get(j, node, key), &result);
    return result;
}
BUSTER_GLOBAL_LOCAL void cm_quote(FILE *f, const char *s)
{
    fputc('"', f);
    for (size_t i = 0; s && s[i]; ++i)
    {
        unsigned c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { fputc('\\', f); fputc((int)c, f); }
        else if (c < 32 || c == 127) fprintf(f, "\\u%04x", c);
        else fputc((int)c, f);
    }
    fputc('"', f);
}
BUSTER_GLOBAL_LOCAL void cm_escape(FILE *f, const char *s, int csv)
{
    if (csv) fputc('"', f);
    if (csv && s && s[0] && (strchr("=+-@\t\r\n", s[0]) || s[0] == ' ')) fputc('\'', f);
    for (size_t i = 0; s && s[i]; ++i)
    {
        unsigned c = (unsigned char)s[i];
        if (csv && c == '"') fputs("\"\"", f);
        else if (csv) fputc((int)c, f);
        else if (c < 32 || c == 127 || c == '|' || c == '<' || c == '>' || c == '&' ||
                 c == '[' || c == ']' || c == '(' || c == ')' || c == 96 || c == '\\' || c == '*' || c == '_')
            fprintf(f, "&#%u;", c);
        else fputc((int)c, f);
    }
    if (csv) fputc('"', f);
}
BUSTER_GLOBAL_LOCAL void cm_normalize(char *out, size_t capacity, const char *s)
{
    size_t n = 0; int space = 0;
    for (size_t i = 0; s && s[i] && n + 1 < capacity; ++i)
    {
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') space = n > 0;
        else { if (space && n + 2 < capacity) out[n++] = ' '; space = 0; out[n++] = s[i]; }
    }
    if (capacity) out[n] = 0;
}
BUSTER_GLOBAL_LOCAL int64_t cm_time(const char *s)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, count = 0;
    int valid = s && sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute, &second, &count) == 6;
    valid = valid && count == 19 && year >= 2000 && year <= 2199 && month >= 1 && month <= 12 &&
        hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
    unsigned leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    valid = valid && day >= 1 && day <= (month >= 1 && month <= 12 ? months[month - 1] + (month == 2 && leap) : 0);
    size_t p = 19;
    if (valid && s[p] == '.') { ++p; valid = s[p] >= '0' && s[p] <= '9'; while (s[p] >= '0' && s[p] <= '9') ++p; }
    valid = valid && s[p] == 'Z' && s[p + 1] == 0;
    int64_t result = -1;
    if (valid)
    {
        int64_t days = 0;
        for (int y = 1970; y < year; ++y) days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        for (int m = 1; m < month; ++m) days += months[m - 1] + (m == 2 && leap);
        days += day - 1; result = days * 86400 + hour * 3600 + minute * 60 + second;
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_span(const char *start, const char *end, int64_t *seconds)
{
    int64_t a = cm_time(start), b = cm_time(end);
    int result = a >= 0 && b >= a && b - a <= 7 * 86400;
    if (result) *seconds = b - a;
    return result;
}
BUSTER_GLOBAL_LOCAL char *cm_read(const char *path, size_t limit, size_t *length)
{
    char *result = NULL;
    FILE *f = fopen(path, "rb");
    if (f)
    {
        if (fseek(f, 0, SEEK_END) == 0)
        {
            long size = ftell(f);
            if (size >= 0 && (uint64_t)size <= limit && fseek(f, 0, SEEK_SET) == 0)
            {
                result = malloc((size_t)size + 1);
                if (result)
                {
                    *length = fread(result, 1, (size_t)size, f);
                    if (*length == (size_t)size && !ferror(f)) result[*length] = 0;
                    else { free(result); result = NULL; }
                }
            }
        }
        fclose(f);
    }
    return result;
}
#endif
