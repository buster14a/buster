/* #881-E production result composer (A1): lane D's published streams -> the
 * #511 binding's sealed result. See retirement_compose.h for the contract.
 *
 * Map (searchable symbols):
 *   helpers        tp_compose_cursor_*, tp_compose_field, tp_compose_json_number,
 *                  tp_compose_repr, tp_compose_decimal_ratio
 *   family         tp_compose_layout_check, tp_compose_family_build,
 *                  tp_compose_family_free, tp_compose_bounds_of
 *   readers        TpComposeReader, tp_compose_reader_*, TpComposeTiling,
 *                  tp_compose_tiling_*
 *   inputs         tp_compose_inventory, tp_compose_prior, tp_compose_transcript,
 *                  tp_compose_untimed, tp_compose_samples, tp_compose_partitions
 *   outputs        TpComposeWriter, tp_compose_manifests, tp_compose_code,
 *                  tp_compose_series, tp_compose_adapter, tp_compose_receipt,
 *                  tp_compose_bundle, tp_compose_seal
 *   entry points   tp_retirement_compose_bounds, tp_retirement_compose_plan,
 *                  tp_retirement_compose
 *
 * The line formats checked here are lane D's canonical encoders
 * (retirement_execution.h, retirement_samples.h, retirement_untimed.h). The
 * composer re-reads every input through its sealed store inode, rehashes it
 * and cross-checks the frozen schedule with D's own #619 cursor; the binding
 * validator remains the independent authority over every semantic join.
 */
#define _GNU_SOURCE 1
#include "retirement_compose.h"
#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* "ratio=" + the longest %.17g double (23 bytes) + LF, rounded up. */
#define TP_COMPOSE_RATIO_LINE_BYTES 32u
/* One adapter member header line: a 127-byte identity and six numbers. */
#define TP_COMPOSE_MEMBER_LINE_BYTES 256u
/* The series header line and the member terminator ("end\n"). */
#define TP_COMPOSE_SERIES_HEADER_BYTES 256u
#define TP_COMPOSE_SERIES_END_BYTES 4u
/* One adapter result member: identity, indexes and nine bound numbers. */
#define TP_COMPOSE_REPLAY_MEMBER_BYTES 1024u
#define TP_COMPOSE_REPLAY_FIXED_BYTES 256u
/* #615 manifest: fixed envelope plus one shard descriptor (identity, path,
 * byte count and digest) per numeric shard. */
#define TP_COMPOSE_MANIFEST_FIXED_BYTES 256u
#define TP_COMPOSE_MANIFEST_SHARD_BYTES 384u
/* The result bundle holds at most three manifest descriptors. */
#define TP_COMPOSE_BUNDLE_BYTES 16384u
/* The sealed-result record: envelope plus one {name,path,bytes,sha256}. */
#define TP_COMPOSE_SEAL_FIXED_BYTES 2048u
#define TP_COMPOSE_SEAL_ENTRY_BYTES 512u
#define TP_COMPOSE_TEMPLATE_BYTES (UINT64_C(1) << 20)
#define TP_COMPOSE_NUMBER_BYTES 48u
#define TP_COMPOSE_LINE_BYTES 8192u
/* Decimal('a') / Decimal('b') under Python's default 28-digit context. */
#define TP_COMPOSE_DECIMAL_DIGITS 28u
/* Code-byte ratios are computed in doubles; exact below 2^53. */
#define TP_COMPOSE_CODE_BYTES_MAX (UINT64_C(1) << 53)
/* Code-byte totals stay below 2^60, so decimal long division never wraps. */
#define TP_COMPOSE_TOTAL_BYTES_MAX (UINT64_C(1) << 60)
#define TP_COMPOSE_PLACEHOLDER \
    "\"raw_measurements_sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\""
#define TP_COMPOSE_EMPTY_SHA256 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define TP_COMPOSE_NONE 0xffffffffu

static char const* const tp_compose_metric_names[TP_RETIREMENT_COMPOSE_METRICS] = {
    "compiler_wall_time", "compiler_peak_memory", "generated_runtime",
    "compiler_batch_wall_time", "compiler_batch_peak_rss"};
static char const* const tp_compose_aggregate_limits[TP_RETIREMENT_COMPOSE_METRICS] = {
    "1.02", "1.02", "1.03", "1.02", "1.02"};
static char const* const tp_compose_cell_limits[TP_RETIREMENT_COMPOSE_METRICS] = {
    "1.05", "1.05", "1.03", "1.05", "1.05"};
static char const* const tp_compose_dimension_names[TP_RETIREMENT_COMPOSE_DIMENSIONS] = {
    "target", "cpu", "allocator", "frontend_lowering", "PIC", "artifact_stage"};

/* ---------------------------------------------------------------- helpers */

static int tp_compose_digest(char const* text)
{
    int valid = text && strnlen(text, 65) == 64;
    for (unsigned i = 0; valid && i < 64; ++i)
        valid = (text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f');
    return valid;
}

/* A JSON-safe printable identifier: no quote, backslash or control byte. */
static int tp_compose_printable(char const* text, size_t capacity)
{
    size_t length = text ? strnlen(text, capacity + 1) : 0;
    int valid = length && length <= capacity;
    for (size_t i = 0; valid && i < length; ++i)
        valid = text[i] > 0x20 && text[i] < 0x7f && text[i] != '"' && text[i] != '\\';
    return valid;
}

/* A normalized relative evidence path (the validator's _relative_path). */
static int tp_compose_relative_path(char const* path)
{
    size_t length = path ? strnlen(path, TP_RETIREMENT_STORE_PATH_BYTES + 1) : 0;
    int valid = tp_compose_printable(path, TP_RETIREMENT_STORE_PATH_BYTES) && path[0] != '/';
    size_t start = 0;
    for (size_t i = 0; valid && i <= length; ++i)
        if (i == length || path[i] == '/')
        {
            size_t count = i - start;
            valid = count && !(count == 1 && path[start] == '.') &&
                    !(count == 2 && path[start] == '.' && path[start + 1] == '.');
            start = i + 1;
        }
    return valid;
}

/* The adapter member token domain: [A-Za-z0-9][A-Za-z0-9_.:/=-]*. */
static int tp_compose_member_text(char const* text, int first)
{
    size_t length = text ? strlen(text) : 0;
    int valid = length > 0;
    for (size_t i = 0; valid && i < length; ++i)
    {
        char c = text[i];
        int alphanumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        valid = alphanumeric || ((!first || i) && (c == '_' || c == '.' || c == ':' || c == '/' || c == '=' || c == '-'));
    }
    return valid;
}

static void tp_compose_sha_hex(void const* bytes, size_t length, char output[65])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes, (u64)length);
    sha256_finish_hex(&hash, (char8*)output);
}

typedef struct TpComposeCursor
{
    char const* bytes;
    size_t length, offset;
    int valid;
} TpComposeCursor;

static void tp_compose_cursor_literal(TpComposeCursor* cursor, char const* literal)
{
    size_t length = strlen(literal);
    cursor->valid = cursor->valid && cursor->offset <= cursor->length && length <= cursor->length - cursor->offset &&
                    !memcmp(cursor->bytes + cursor->offset, literal, length);
    if (cursor->valid) cursor->offset += length;
}

static void tp_compose_cursor_u64(TpComposeCursor* cursor, uint64_t* output)
{
    size_t start = cursor->offset;
    uint64_t value = 0;
    int valid = cursor->valid && start < cursor->length && cursor->bytes[start] >= '0' && cursor->bytes[start] <= '9';
    while (valid && cursor->offset < cursor->length && cursor->bytes[cursor->offset] >= '0' &&
           cursor->bytes[cursor->offset] <= '9')
    {
        unsigned digit = (unsigned)(cursor->bytes[cursor->offset] - '0');
        valid = value <= (UINT64_MAX - digit) / 10;
        if (valid)
        {
            value = value * 10 + digit;
            ++cursor->offset;
        }
    }
    valid = valid && (cursor->offset == start + 1 || cursor->bytes[start] != '0');
    cursor->valid = valid;
    *output = valid ? value : 0;
}

/* A string body up to (not including) its closing quote, which is consumed. */
static void tp_compose_cursor_text(TpComposeCursor* cursor, char* output, size_t capacity)
{
    size_t start = cursor->offset;
    while (cursor->valid && cursor->offset < cursor->length && cursor->bytes[cursor->offset] != '"' &&
           cursor->bytes[cursor->offset] != '\\' && cursor->offset - start < capacity)
        ++cursor->offset;
    size_t length = cursor->offset - start;
    cursor->valid = cursor->valid && length < capacity && cursor->offset < cursor->length &&
                    cursor->bytes[cursor->offset] == '"';
    if (cursor->valid)
    {
        memcpy(output, cursor->bytes + start, length);
        output[length] = 0;
        ++cursor->offset;
    }
    else if (capacity) output[0] = 0;
}

/* One finite positive JSON number: (0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?. */
static int tp_compose_json_number(char const* text, size_t length, double* value)
{
    size_t i = 0;
    int valid = text && length && length < TP_COMPOSE_NUMBER_BYTES;
    if (valid && text[0] == '0') i = 1;
    else if (valid)
    {
        valid = text[0] >= '1' && text[0] <= '9';
        while (valid && i < length && text[i] >= '0' && text[i] <= '9') ++i;
    }
    if (valid && i < length && text[i] == '.')
    {
        size_t start = ++i;
        while (i < length && text[i] >= '0' && text[i] <= '9') ++i;
        valid = i > start;
    }
    if (valid && i < length && (text[i] == 'e' || text[i] == 'E'))
    {
        ++i;
        if (i < length && (text[i] == '+' || text[i] == '-')) ++i;
        size_t start = i;
        while (i < length && text[i] >= '0' && text[i] <= '9') ++i;
        valid = i > start;
    }
    valid = valid && i == length;
    double parsed = 0.0;
    if (valid)
    {
        char buffer[TP_COMPOSE_NUMBER_BYTES];
        memcpy(buffer, text, length);
        buffer[length] = 0;
        char* end = NULL;
        errno = 0;
        parsed = strtod(buffer, &end);
        valid = end == buffer + length && errno == 0 && isfinite(parsed) && parsed > 0.0;
    }
    *value = valid ? parsed : 0.0;
    return valid;
}

static void tp_compose_cursor_number(TpComposeCursor* cursor, double* value)
{
    size_t start = cursor->offset;
    while (cursor->valid && cursor->offset < cursor->length && cursor->bytes[cursor->offset] != ',' &&
           cursor->bytes[cursor->offset] != '}')
        ++cursor->offset;
    cursor->valid = cursor->valid && tp_compose_json_number(cursor->bytes + start, cursor->offset - start, value);
    if (!cursor->valid) *value = 0.0;
}

/* Locate a top-level `"key":` in one canonical JSON object line and return
 * its value span. Keys are unique and values carry no escapes. */
static int tp_compose_field(char const* line, size_t length, char const* key, char const** value, size_t* value_length)
{
    size_t key_length = strlen(key), i = 0, start = 0, end = 0;
    unsigned depth = 0;
    int found = 0, valid = line && length && line[0] == '{' && !memchr(line, '\\', length);
    while (valid && !found && i < length)
    {
        char c = line[i];
        if (c == '"')
        {
            size_t text = ++i;
            while (i < length && line[i] != '"') ++i;
            valid = i < length;
            size_t text_end = i++;
            if (valid && depth == 1 && i < length && line[i] == ':' && text_end - text == key_length &&
                !memcmp(line + text, key, key_length))
            {
                start = end = i + 1;
                unsigned nested = 0;
                int quoted = 0, closed = 0;
                while (end < length && !closed)
                {
                    char v = line[end];
                    if (v == '"') quoted = !quoted;
                    else if (!quoted && (v == '{' || v == '[')) ++nested;
                    else if (!quoted && (v == '}' || v == ']') && nested) --nested;
                    else if (!quoted && !nested && (v == ',' || v == '}')) closed = 1;
                    if (!closed) ++end;
                }
                valid = closed && end > start;
                found = valid;
            }
        }
        else
        {
            if (c == '{' || c == '[') ++depth;
            else if ((c == '}' || c == ']') && depth) --depth;
            ++i;
        }
    }
    valid = valid && found;
    *value = valid ? line + start : NULL;
    *value_length = valid ? end - start : 0;
    return valid;
}

static int tp_compose_field_u64(char const* line, size_t length, char const* key, uint64_t* output)
{
    char const* value = NULL;
    size_t count = 0;
    TpComposeCursor cursor = {0};
    int valid = tp_compose_field(line, length, key, &value, &count);
    if (valid)
    {
        cursor = (TpComposeCursor){value, count, 0, 1};
        tp_compose_cursor_u64(&cursor, output);
        valid = cursor.valid && cursor.offset == count;
    }
    if (!valid) *output = 0;
    return valid;
}

static int tp_compose_field_is(char const* line, size_t length, char const* key, char const* expected)
{
    char const* value = NULL;
    size_t count = 0;
    int valid = tp_compose_field(line, length, key, &value, &count) && count == strlen(expected) &&
                !memcmp(value, expected, count);
    return valid;
}

/* A quoted string field copied without its quotes. */
static int tp_compose_field_text(char const* line, size_t length, char const* key, char* output, size_t capacity)
{
    char const* value = NULL;
    size_t count = 0;
    int valid = tp_compose_field(line, length, key, &value, &count) && count >= 2 && value[0] == '"' &&
                value[count - 1] == '"' && count - 2 < capacity;
    if (valid)
    {
        memcpy(output, value + 1, count - 2);
        output[count - 2] = 0;
    }
    else if (capacity) output[0] = 0;
    return valid;
}

/* A number field or `null` (none == 1) when the frozen identity is absent. */
static int tp_compose_field_optional(char const* line, size_t length, char const* key, int none, uint64_t expected)
{
    uint64_t value = 0;
    int valid = none ? tp_compose_field_is(line, length, key, "null") :
                tp_compose_field_u64(line, length, key, &value) && value == expected;
    return valid;
}

/* Python's repr(float) for a finite nonnegative double: the shortest
 * round-tripping digits, fixed notation for -4 < decpt <= 16, otherwise
 * d.ddde+XX. json.dumps writes floats with this repr. */
static int tp_compose_repr(double value, char output[40])
{
    int valid = isfinite(value) && value >= 0.0;
    uint64_t mantissa = 0;
    int exponent = 0;
    unsigned digits = 0;
    if (valid && value == 0.0)
    {
        strcpy(output, "0.0");
        digits = 0;
    }
    else if (valid)
    {
        int found = 0;
        for (unsigned precision = 1; !found && precision <= 17; ++precision)
        {
            char text[40], candidate[48];
            snprintf(text, sizeof(text), "%.*e", (int)precision - 1, value);
            uint64_t parsed = 0;
            int exp10 = 0;
            char const* cursor = text;
            while (*cursor && *cursor != 'e')
            {
                if (*cursor >= '0' && *cursor <= '9') parsed = parsed * 10 + (uint64_t)(*cursor - '0');
                ++cursor;
            }
            if (*cursor == 'e') exp10 = atoi(cursor + 1);
            uint64_t low = 1;
            for (unsigned i = 1; i < precision; ++i) low *= 10;
            uint64_t high = low * 10;
            for (int delta = 0; !found && delta < 3; ++delta)
            {
                uint64_t trial = delta == 0 ? parsed : delta == 1 ? parsed - 1 : parsed + 1;
                if (trial >= low && trial < high)
                {
                    snprintf(candidate, sizeof(candidate), "%" PRIu64 "e%d", trial, exp10 - (int)(precision - 1));
                    if (strtod(candidate, NULL) == value)
                    {
                        found = 1;
                        mantissa = trial;
                        exponent = exp10;
                        digits = precision;
                    }
                }
            }
        }
        valid = found;
    }
    if (valid && digits)
    {
        char text[24];
        snprintf(text, sizeof(text), "%" PRIu64, mantissa);
        while (digits > 1 && text[digits - 1] == '0') text[--digits] = 0;
        int point = exponent + 1;
        size_t used = 0;
        if (point > -4 && point <= 16)
        {
            if (point <= 0)
            {
                used += (size_t)snprintf(output + used, 40 - used, "0.");
                for (int i = 0; i < -point; ++i) output[used++] = '0';
                used += (size_t)snprintf(output + used, 40 - used, "%s", text);
            }
            else if ((unsigned)point >= digits)
            {
                used += (size_t)snprintf(output + used, 40 - used, "%s", text);
                for (unsigned i = digits; i < (unsigned)point; ++i) output[used++] = '0';
                used += (size_t)snprintf(output + used, 40 - used, ".0");
            }
            else
                used += (size_t)snprintf(output + used, 40 - used, "%.*s.%s", point, text, text + point);
        }
        else if (digits > 1)
            used += (size_t)snprintf(output + used, 40 - used, "%c.%se%c%02d", text[0], text + 1,
                                     exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
        else
            used += (size_t)snprintf(output + used, 40 - used, "%ce%c%02d", text[0], exponent < 0 ? '-' : '+',
                                     exponent < 0 ? -exponent : exponent);
        output[used] = 0;
    }
    if (!valid) output[0] = 0;
    return valid;
}

/* float(Decimal(numerator) / Decimal(denominator)) under the default
 * 28-digit ROUND_HALF_EVEN context, then correctly rounded to a double. */
static int tp_compose_decimal_ratio(uint64_t numerator, uint64_t denominator, double* value)
{
    /* remainder * 10 stays below 2^64 for these operand bounds. */
    int valid = denominator > 0 && numerator <= TP_COMPOSE_TOTAL_BYTES_MAX && denominator <= TP_COMPOSE_TOTAL_BYTES_MAX;
    unsigned char digits[TP_COMPOSE_DECIMAL_DIGITS + 24];
    unsigned count = 0;
    int point = 0;
    uint64_t quotient = valid ? numerator / denominator : 0;
    uint64_t remainder = valid ? numerator % denominator : 0;
    if (valid && quotient)
    {
        char text[48];
        snprintf(text, sizeof(text), "%" PRIu64, quotient);
        for (char const* cursor = text; *cursor; ++cursor) digits[count++] = (unsigned char)(*cursor - '0');
        point = (int)count;
    }
    while (valid && remainder && count < TP_COMPOSE_DECIMAL_DIGITS)
    {
        remainder *= 10;
        unsigned digit = (unsigned)(remainder / denominator);
        remainder %= denominator;
        if (!count && !digit) --point;
        else digits[count++] = (unsigned char)digit;
    }
    if (valid && remainder && count)
    {
        remainder *= 10;
        unsigned next = (unsigned)(remainder / denominator);
        int sticky = remainder % denominator != 0;
        int up = next > 5 || (next == 5 && (sticky || (digits[count - 1] & 1)));
        for (unsigned i = count; up && i > 0; --i)
        {
            if (digits[i - 1] == 9) digits[i - 1] = 0;
            else
            {
                ++digits[i - 1];
                up = 0;
            }
        }
        if (up)
        {
            digits[0] = 1;
            for (unsigned i = 1; i < count; ++i) digits[i] = 0;
            ++point;
        }
    }
    double parsed = 0.0;
    if (valid && count)
    {
        char text[TP_COMPOSE_DECIMAL_DIGITS + 32];
        size_t used = 0;
        text[used++] = '0';
        text[used++] = '.';
        for (unsigned i = 0; i < count; ++i) text[used++] = (char)('0' + digits[i]);
        snprintf(text + used, sizeof(text) - used, "e%d", point);
        parsed = strtod(text, NULL);
        valid = isfinite(parsed);
    }
    *value = valid ? parsed : 0.0;
    return valid;
}

/* Iterative bottom-up merge sort of `order` by name (strcmp, which is
 * Python's code-point order for these ASCII names). */
static int tp_compose_sort(unsigned* order, unsigned count, char const* names, size_t stride)
{
    unsigned* scratch = count ? (unsigned*)malloc((size_t)count * sizeof(*scratch)) : NULL;
    int valid = !count || scratch;
    for (unsigned width = 1; valid && width < count; width *= 2)
    {
        for (unsigned left = 0; left < count; left += 2 * width)
        {
            unsigned middle = left + width < count ? left + width : count;
            unsigned right = left + 2 * width < count ? left + 2 * width : count;
            unsigned a = left, b = middle, out = left;
            while (a < middle || b < right)
            {
                int take_left = b >= right ||
                    (a < middle && strcmp(names + (size_t)order[a] * stride, names + (size_t)order[b] * stride) <= 0);
                scratch[out++] = take_left ? order[a++] : order[b++];
            }
        }
        memcpy(order, scratch, (size_t)count * sizeof(*order));
    }
    free(scratch);
    return valid;
}

/* ----------------------------------------------------------------- family */

typedef struct TpComposeMember
{
    char name[TP_RETIREMENT_COMPOSE_MEMBER_BYTES];
    unsigned metric, kind, family, dimension, unit, cells;
    char const* value;
} TpComposeMember;

typedef struct TpComposeFamily
{
    TpRetirementComposeLayout const* layout;
    TpComposeMember* members;
    unsigned* order;
    unsigned* group_first;
    unsigned* object_groups;
    unsigned* runtime_dense;
    unsigned* runtime_ids;
    unsigned* runtime_index;
    unsigned count, bootstrap, cells_total, object_count, runtime_count;
    unsigned cells[TP_RETIREMENT_COMPOSE_METRICS];
} TpComposeFamily;

/* The frozen A1 shape: ascending rows, groups numbered by smallest member,
 * singletons of one non-object row, object rows sharing configuration. */
static int tp_compose_layout_check(TpRetirementComposeLayout const* layout)
{
    int valid = layout && layout->rows && layout->group_kinds && layout->row_count && layout->group_count &&
                layout->row_count <= TP_RETIREMENT_MAX_CELLS && layout->group_count <= layout->row_count &&
                layout->population_rows <= TP_RETIREMENT_MAX_CELLS && layout->untimed_groups <= TP_RETIREMENT_MAX_CELLS;
    unsigned* first = valid ? (unsigned*)malloc((size_t)layout->group_count * 2 * sizeof(*first)) : NULL;
    unsigned* members = first ? first + layout->group_count : NULL;
    valid = valid && first;
    for (unsigned g = 0; valid && g < layout->group_count; ++g)
    {
        first[g] = TP_COMPOSE_NONE;
        members[g] = 0;
        valid = layout->group_kinds[g] == TP_RETIREMENT_GROUP_OBJECT ||
                layout->group_kinds[g] == TP_RETIREMENT_GROUP_SINGLETON;
    }
    unsigned next = 0;
    for (unsigned r = 0; valid && r < layout->row_count; ++r)
    {
        TpRetirementComposeRow const* row = layout->rows + r;
        valid = row->id < layout->population_rows && (!r || row->id > layout->rows[r - 1].id) &&
                row->group < layout->group_count && row->runtime <= 1;
        for (unsigned d = 0; valid && d < TP_RETIREMENT_COMPOSE_DIMENSIONS; ++d)
            valid = row->dimensions[d] && strlen(row->dimensions[d]) <= 64 &&
                    tp_compose_member_text(row->dimensions[d], 0);
        if (valid && first[row->group] == TP_COMPOSE_NONE)
        {
            valid = row->group == next++;
            first[row->group] = r;
        }
        if (valid)
        {
            ++members[row->group];
            int object = layout->group_kinds[row->group] == TP_RETIREMENT_GROUP_OBJECT;
            TpRetirementComposeRow const* lead = layout->rows + first[row->group];
            valid = object ? !row->runtime && !strcmp(row->dimensions[5], "object") :
                             strcmp(row->dimensions[5], "object") != 0;
            for (unsigned d = 0; valid && object && d < TP_RETIREMENT_COMPOSE_DIMENSIONS; ++d)
                valid = !strcmp(row->dimensions[d], lead->dimensions[d]);
        }
    }
    valid = valid && next == layout->group_count;
    for (unsigned g = 0; valid && g < layout->group_count; ++g)
        valid = layout->group_kinds[g] == TP_RETIREMENT_GROUP_OBJECT || members[g] == 1;
    free(first);
    return valid;
}

static void tp_compose_family_free(TpComposeFamily* family)
{
    if (family)
    {
        free(family->members);
        free(family->order);
        free(family->group_first);
        *family = (TpComposeFamily){0};
    }
}

/* Dimension `d` of cell `cell` of metric `metric`: a timed row's identity or,
 * for the batch pair, its object group's (the group's first member). */
static char const* tp_compose_cell_value(TpComposeFamily const* family, unsigned metric, unsigned cell, unsigned d)
{
    TpRetirementComposeLayout const* layout = family->layout;
    unsigned dense = metric < 2 ? cell : metric == 2 ? family->runtime_dense[cell] :
                     family->group_first[family->object_groups[cell]];
    return layout->rows[dense].dimensions[d];
}

static int tp_compose_member_add(TpComposeFamily* family, unsigned capacity, unsigned metric, unsigned kind,
                                 unsigned dimension, unsigned unit, char const* value, char const* format, ...)
{
    int valid = family->count < capacity;
    if (valid)
    {
        TpComposeMember* member = family->members + family->count;
        va_list arguments;
        va_start(arguments, format);
        int length = vsnprintf(member->name, sizeof(member->name), format, arguments);
        va_end(arguments);
        valid = length > 0 && (size_t)length < sizeof(member->name) && tp_compose_member_text(member->name, 1);
        if (valid)
        {
            member->metric = metric;
            member->kind = kind;
            member->dimension = dimension;
            member->unit = unit;
            member->value = value;
            member->cells = 0;
            ++family->count;
        }
    }
    return valid;
}

/* The validator's _derive_statistical_family over the timed projection:
 * per metric an aggregate, one slice per present dimension value, one cell
 * per eligible row (wall/memory: timed rows; runtime: runtime rows) or
 * object group (the batch pair); members sorted; bootstrap and cell family
 * indexes are the ordinals among aggregate/slice and cell members. */
static int tp_compose_family_build(TpRetirementComposeLayout const* layout, TpComposeFamily* family)
{
    *family = (TpComposeFamily){.layout = layout};
    int valid = tp_compose_layout_check(layout);
    unsigned rows = valid ? layout->row_count : 0, groups = valid ? layout->group_count : 0;
    size_t slots = valid ? (size_t)groups * 2 + (size_t)rows * 3 : 0;
    family->group_first = valid ? (unsigned*)malloc(slots * sizeof(unsigned)) : NULL;
    valid = valid && family->group_first;
    if (valid)
    {
        family->object_groups = family->group_first + groups;
        family->runtime_dense = family->object_groups + groups;
        family->runtime_ids = family->runtime_dense + rows;
        family->runtime_index = family->runtime_ids + rows;
        for (unsigned g = 0; g < groups; ++g) family->group_first[g] = TP_COMPOSE_NONE;
        for (unsigned r = 0; r < rows; ++r)
        {
            TpRetirementComposeRow const* row = layout->rows + r;
            family->runtime_index[r] = TP_COMPOSE_NONE;
            if (family->group_first[row->group] == TP_COMPOSE_NONE) family->group_first[row->group] = r;
            if (row->runtime)
            {
                family->runtime_index[r] = family->runtime_count;
                family->runtime_ids[family->runtime_count] = row->id;
                family->runtime_dense[family->runtime_count++] = r;
            }
        }
        for (unsigned g = 0; g < groups; ++g)
            if (layout->group_kinds[g] == TP_RETIREMENT_GROUP_OBJECT) family->object_groups[family->object_count++] = g;
        family->cells[0] = family->cells[1] = rows;
        family->cells[2] = family->runtime_count;
        family->cells[3] = family->cells[4] = family->object_count;
        /* Every metric needs a cell, as _derive_statistical_family requires. */
        for (unsigned m = 0; valid && m < TP_RETIREMENT_COMPOSE_METRICS; ++m)
        {
            valid = family->cells[m] > 0 && family->cells_total <= TP_RETIREMENT_COMPOSE_CELL_MEMBERS - family->cells[m];
            if (valid) family->cells_total += family->cells[m];
        }
    }
    unsigned capacity = 0;
    for (unsigned m = 0; valid && m < TP_RETIREMENT_COMPOSE_METRICS; ++m)
        capacity += 1 + family->cells[m] * (TP_RETIREMENT_COMPOSE_DIMENSIONS + 1);
    family->members = valid ? (TpComposeMember*)malloc((size_t)capacity * sizeof(*family->members)) : NULL;
    family->order = valid ? (unsigned*)malloc((size_t)capacity * sizeof(*family->order)) : NULL;
    valid = valid && family->members && family->order;
    for (unsigned m = 0; valid && m < TP_RETIREMENT_COMPOSE_METRICS; ++m)
    {
        char const* metric = tp_compose_metric_names[m];
        valid = tp_compose_member_add(family, capacity, m, 0, TP_COMPOSE_NONE, TP_COMPOSE_NONE, NULL,
                                      "%s/aggregate", metric);
        for (unsigned d = 0; valid && d < TP_RETIREMENT_COMPOSE_DIMENSIONS; ++d)
            for (unsigned cell = 0; valid && cell < family->cells[m]; ++cell)
            {
                char const* value = tp_compose_cell_value(family, m, cell, d);
                int seen = 0;
                for (unsigned earlier = 0; !seen && earlier < cell; ++earlier)
                    seen = !strcmp(tp_compose_cell_value(family, m, earlier, d), value);
                if (!seen)
                    valid = tp_compose_member_add(family, capacity, m, 0, d, TP_COMPOSE_NONE, value, "%s/slice/%s=%s",
                                                  metric, tp_compose_dimension_names[d], value);
            }
        for (unsigned cell = 0; valid && cell < family->cells[m]; ++cell)
            valid = m < 2 ? tp_compose_member_add(family, capacity, m, 1, TP_COMPOSE_NONE, cell, NULL,
                                                  "%s/cell/row=%u", metric, layout->rows[cell].id) :
                    m == 2 ? tp_compose_member_add(family, capacity, m, 1, TP_COMPOSE_NONE, cell, NULL,
                                                   "%s/cell/row=%u", metric, family->runtime_ids[cell]) :
                             tp_compose_member_add(family, capacity, m, 1, TP_COMPOSE_NONE, cell, NULL,
                                                   "%s/cell/group=%u", metric, family->object_groups[cell]);
    }
    for (unsigned i = 0; valid && i < family->count; ++i)
    {
        TpComposeMember* member = family->members + i;
        family->order[i] = i;
        if (member->kind) member->cells = 1;
        else if (member->dimension == TP_COMPOSE_NONE) member->cells = family->cells[member->metric];
        else
            for (unsigned cell = 0; cell < family->cells[member->metric]; ++cell)
                member->cells += !strcmp(tp_compose_cell_value(family, member->metric, cell, member->dimension),
                                         member->value);
    }
    valid = valid && tp_compose_sort(family->order, family->count, family->members[0].name, sizeof(TpComposeMember));
    unsigned cells = 0;
    for (unsigned i = 0; valid && i < family->count; ++i)
    {
        TpComposeMember* member = family->members + family->order[i];
        valid = !i || strcmp(family->members[family->order[i - 1]].name, member->name) < 0;
        member->family = member->kind ? cells++ : family->bootstrap++;
    }
    valid = valid && family->bootstrap && family->bootstrap <= TP_RETIREMENT_COMPOSE_BOOTSTRAP_MEMBERS &&
            cells == family->cells_total;
    if (!valid) tp_compose_family_free(family);
    return valid;
}

static int tp_compose_add(uint64_t* total, uint64_t value)
{
    int valid = value <= UINT64_MAX - *total;
    if (valid) *total += value;
    return valid;
}

/* Every composer output's byte bound, each at most one store file. */
static int tp_compose_bounds_of(TpRetirementComposeShape const* shape, TpComposeFamily const* family,
                                TpRetirementComposeBounds* bounds)
{
    TpRetirementComposeLayout const* layout = shape->layout;
    uint64_t per_unit = (uint64_t)TP_RETIREMENT_ROUNDS * shape->pairs;
    uint64_t populations[2] = {(uint64_t)layout->row_count * per_unit, (uint64_t)family->object_count * per_unit};
    TpRetirementComposeBounds result = {0};
    int valid = shape->pairs >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND && shape->pairs <= TP_RETIREMENT_EXECUTION_MAX_PAIRS &&
                !(shape->pairs & 1) && shape->code_rows && shape->code_rows <= TP_RETIREMENT_MAX_CELLS &&
                shape->prior_entries <= TP_RETIREMENT_STORE_FILES;
    for (unsigned p = 0; valid && p < 2; ++p)
    {
        uint64_t records = populations[p];
        uint64_t partitions = (records + TP_RETIREMENT_SAMPLE_PARTITION_RECORDS - 1) / TP_RETIREMENT_SAMPLE_PARTITION_RECORDS;
        uint64_t shards = (records + TP_RETIREMENT_SAMPLE_SHARD_RECORDS - 1) / TP_RETIREMENT_SAMPLE_SHARD_RECORDS;
        valid = records && result.manifest_count + partitions <= TP_RETIREMENT_COMPOSE_PARTITIONS;
        if (valid)
        {
            result.manifest_count += (unsigned)partitions;
            valid = tp_compose_add(&result.manifests, partitions * TP_COMPOSE_MANIFEST_FIXED_BYTES +
                                                      shards * TP_COMPOSE_MANIFEST_SHARD_BYTES);
        }
    }
    uint64_t lines = 0;
    for (unsigned m = 0; valid && m < TP_RETIREMENT_COMPOSE_METRICS; ++m)
        valid = tp_compose_add(&lines, (uint64_t)(TP_RETIREMENT_COMPOSE_DIMENSIONS + 2) * family->cells[m] * per_unit);
    if (valid)
    {
        result.members = family->count;
        result.bootstrap_members = family->bootstrap;
        result.cell_members = family->cells_total;
        result.code = (uint64_t)shape->code_rows * TP_RETIREMENT_CODE_RECORD_BYTES_MAX;
        result.series = TP_COMPOSE_SERIES_HEADER_BYTES +
            (uint64_t)family->count * (TP_COMPOSE_MEMBER_LINE_BYTES + TP_COMPOSE_SERIES_END_BYTES);
        valid = lines <= (UINT64_MAX - result.series) / TP_COMPOSE_RATIO_LINE_BYTES;
        if (valid) result.series += lines * TP_COMPOSE_RATIO_LINE_BYTES;
        result.replay = TP_COMPOSE_REPLAY_FIXED_BYTES + (uint64_t)family->count * TP_COMPOSE_REPLAY_MEMBER_BYTES;
        result.bundle = TP_COMPOSE_BUNDLE_BYTES;
        result.receipt = TP_RETIREMENT_RECEIPT_BYTES;
        result.seal = TP_COMPOSE_SEAL_FIXED_BYTES +
            (uint64_t)(shape->prior_entries + TP_RETIREMENT_STORE_FILES) * TP_COMPOSE_SEAL_ENTRY_BYTES;
        result.files = result.manifest_count + TP_RETIREMENT_COMPOSE_FIXED_OUTPUTS;
    }
    /* A1's single adapter input file is bound by the store's per-file cap:
     * a family too large for it is refused here, before any timing. */
    valid = valid && result.manifests <= TP_RETIREMENT_STORE_FILE_BYTES && result.code <= TP_RETIREMENT_STORE_FILE_BYTES &&
            result.series <= TP_RETIREMENT_STORE_FILE_BYTES && result.replay <= TP_RETIREMENT_STORE_FILE_BYTES &&
            result.seal <= TP_RETIREMENT_STORE_FILE_BYTES;
    valid = valid && tp_compose_add(&result.total, result.manifests) && tp_compose_add(&result.total, result.code) &&
            tp_compose_add(&result.total, result.series) && tp_compose_add(&result.total, result.replay) &&
            tp_compose_add(&result.total, result.bundle) && tp_compose_add(&result.total, result.receipt) &&
            tp_compose_add(&result.total, result.seal);
    *bounds = valid ? result : (TpRetirementComposeBounds){0};
    return valid;
}

int tp_retirement_compose_bounds(TpRetirementComposeShape const* shape, TpRetirementComposeBounds* bounds)
{
    TpComposeFamily family;
    int valid = shape && bounds && tp_compose_family_build(shape->layout, &family);
    if (valid)
    {
        valid = tp_compose_bounds_of(shape, &family, bounds);
        tp_compose_family_free(&family);
    }
    if (bounds && !valid) *bounds = (TpRetirementComposeBounds){0};
    return valid;
}

int tp_retirement_compose_plan(TpRetirementStore* store, TpRetirementCampaignCapacity const* capacity,
    TpRetirementComposeShape const* shape, unsigned retained_files, uint64_t retained_bytes,
    unsigned external_entries, uint64_t external_bytes, TpRetirementCampaignStorePlan* plan)
{
    TpRetirementComposeBounds bounds = {0};
    uint64_t control_bytes = 0;
    int valid = store && capacity && plan && tp_retirement_compose_bounds(shape, &bounds) &&
                retained_files <= TP_RETIREMENT_STORE_FILES - bounds.files &&
                tp_compose_add(&control_bytes, bounds.total) && tp_compose_add(&control_bytes, retained_bytes);
    if (plan) *plan = (TpRetirementCampaignStorePlan){0};
    valid = valid && tp_retirement_campaign_store_preflight(capacity, bounds.files + retained_files, control_bytes,
        external_entries, external_bytes, plan) &&
        plan->owned_files <= store->capacity &&
        tp_retirement_store_plan(store, (unsigned)plan->owned_files, plan->owned_bytes, external_entries, external_bytes);
    if (!valid)
    {
        if (plan) *plan = (TpRetirementCampaignStorePlan){0};
        if (store) store->failed = 1;
    }
    return valid;
}

/* ----------------------------------------------------------------- readers */

typedef struct TpComposeReader
{
    FILE* stream;
    TpRetirementStoredFile const* entry;
    Sha256 hash;
    uint64_t bytes, lines;
    int failed;
} TpComposeReader;

static int tp_compose_reader_open(TpComposeReader* reader, TpRetirementStore* store, char const* path)
{
    *reader = (TpComposeReader){0};
    int fd = tp_retirement_store_read(store, path, &reader->entry);
    reader->stream = fd >= 0 ? fdopen(fd, "rb") : NULL;
    if (fd >= 0 && !reader->stream) close(fd);
    int valid = reader->stream != NULL;
    sha256_init(&reader->hash);
    reader->failed = !valid;
    return valid;
}

/* 1 for one LF-terminated line, 0 at a clean end of file, -1 otherwise. */
static int tp_compose_reader_line(TpComposeReader* reader, char* line, size_t capacity, size_t* length)
{
    size_t used = 0;
    int result = -1, done = reader->failed || !reader->stream;
    while (!done)
    {
        int c = getc_unlocked(reader->stream);
        if (c == EOF)
        {
            result = !used && !ferror(reader->stream) ? 0 : -1;
            done = 1;
        }
        else if (!c || used + 1 >= capacity) done = 1;
        else
        {
            line[used++] = (char)c;
            if (c == '\n')
            {
                result = 1;
                done = 1;
            }
        }
    }
    if (result == 1)
    {
        line[used] = 0;
        sha256_add(&reader->hash, line, (u64)used);
        reader->bytes += used;
        ++reader->lines;
    }
    *length = result == 1 ? used : 0;
    if (result < 0) reader->failed = 1;
    return result;
}

/* Read exactly `count` raw bytes, hashing them into the file and `extra`. */
static int tp_compose_reader_bytes(TpComposeReader* reader, uint64_t count, Sha256* extra)
{
    unsigned char buffer[65536];
    int valid = !reader->failed && reader->stream;
    while (valid && count)
    {
        size_t want = count < sizeof(buffer) ? (size_t)count : sizeof(buffer);
        size_t got = fread(buffer, 1, want, reader->stream);
        valid = got == want;
        if (valid)
        {
            sha256_add(&reader->hash, buffer, (u64)got);
            sha256_add(extra, buffer, (u64)got);
            reader->bytes += got;
            count -= got;
        }
    }
    if (!valid) reader->failed = 1;
    return valid;
}

/* Close after requiring the sealed byte count, end of file and digest. */
static int tp_compose_reader_close(TpComposeReader* reader)
{
    char digest[65];
    int valid = !reader->failed && reader->stream && reader->entry && getc_unlocked(reader->stream) == EOF &&
                !ferror(reader->stream) && reader->bytes == reader->entry->bytes;
    if (valid)
    {
        sha256_finish_hex(&reader->hash, (char8*)digest);
        valid = !strcmp(digest, reader->entry->sha256);
    }
    if (reader->stream && fclose(reader->stream) != 0) valid = 0;
    reader->stream = NULL;
    return valid;
}

static void tp_compose_reader_abandon(TpComposeReader* reader)
{
    if (reader->stream) fclose(reader->stream);
    reader->stream = NULL;
    reader->failed = 1;
}

/* One writer's metrics shards: artifacts must tile each declared shard from
 * offset zero in record order, shards in declared order (tag, 0000...), with
 * no declared shard left unreferenced or partly referenced. */
typedef struct TpComposeTiling
{
    TpRetirementStore* store;
    char const* const* paths;
    unsigned count, next;
    TpComposeReader reader;
    int open, failed;
    uint64_t artifacts;
} TpComposeTiling;

static int tp_compose_shard_tag(char const* path, char tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1], unsigned* index)
{
    size_t length = path ? strnlen(path, TP_RETIREMENT_METRICS_PATH_CAP) : 0;
    int valid = tp_retirement_metrics_shard_leaf(path);
    size_t tag_length = valid ? length - 9 - 19 : 0;
    if (valid)
    {
        memcpy(tag, path + 19, tag_length);
        tag[tag_length] = 0;
        *index = (unsigned)atoi(path + length - 8);
    }
    return valid;
}

static int tp_compose_tiling_init(TpComposeTiling* tiling, TpRetirementStore* store, char const* const* paths,
                                  unsigned count, char tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1])
{
    *tiling = (TpComposeTiling){.store = store, .paths = paths, .count = count};
    int valid = count <= TP_RETIREMENT_METRICS_SHARDS && (!count || paths);
    tag[0] = 0;
    for (unsigned i = 0; valid && i < count; ++i)
    {
        char shard_tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1];
        unsigned index = 0;
        valid = tp_compose_shard_tag(paths[i], shard_tag, &index) && index == i && (!i || !strcmp(shard_tag, tag));
        if (valid && !i) strcpy(tag, shard_tag);
    }
    tiling->failed = !valid;
    return valid;
}

static int tp_compose_tiling_add(TpComposeTiling* tiling, char const* path, uint64_t offset, uint64_t bytes,
                                 char const* sha256)
{
    int valid = !tiling->failed && bytes && bytes <= TP_RETIREMENT_METRICS_ARTIFACT_BYTES && tp_compose_digest(sha256);
    int same = valid && tiling->open && !strcmp(path, tiling->reader.entry->path);
    if (valid && !same)
    {
        if (tiling->open) valid = tp_compose_reader_close(&tiling->reader);
        tiling->open = 0;
        valid = valid && tiling->next < tiling->count && !strcmp(path, tiling->paths[tiling->next]) && offset == 0 &&
                tp_compose_reader_open(&tiling->reader, tiling->store, tiling->paths[tiling->next]);
        if (valid)
        {
            tiling->open = 1;
            ++tiling->next;
        }
    }
    valid = valid && offset == tiling->reader.bytes && bytes <= tiling->reader.entry->bytes - offset;
    if (valid)
    {
        Sha256 hash;
        char digest[65];
        sha256_init(&hash);
        valid = tp_compose_reader_bytes(&tiling->reader, bytes, &hash);
        if (valid)
        {
            sha256_finish_hex(&hash, (char8*)digest);
            valid = !strcmp(digest, sha256);
        }
        tiling->artifacts += valid;
    }
    if (!valid)
    {
        tiling->failed = 1;
        if (tiling->open) tp_compose_reader_abandon(&tiling->reader);
        tiling->open = 0;
    }
    return valid;
}

static int tp_compose_tiling_finish(TpComposeTiling* tiling)
{
    int valid = !tiling->failed;
    if (tiling->open) valid = tp_compose_reader_close(&tiling->reader) && valid;
    tiling->open = 0;
    valid = valid && tiling->next == tiling->count;
    tiling->failed = !valid;
    return valid;
}

static void tp_compose_tiling_abandon(TpComposeTiling* tiling)
{
    if (tiling->open) tp_compose_reader_abandon(&tiling->reader);
    tiling->open = 0;
    tiling->failed = 1;
}

/* `{"bytes":B,"offset":O,"path":"P","sha256":"S"}` or `null`. expected is 1
 * (an artifact is required), 0 (null is required) or -1 (either). */
static int tp_compose_metrics_field(char const* line, size_t length, int expected, TpComposeTiling* tiling)
{
    char const* value = NULL;
    size_t count = 0;
    int valid = tp_compose_field(line, length, "metrics_artifact", &value, &count);
    int null = valid && count == 4 && !memcmp(value, "null", 4);
    if (valid && null) valid = expected != 1;
    else if (valid && !expected) valid = 0;
    else if (valid)
    {
        uint64_t bytes = 0, offset = 0;
        char path[TP_RETIREMENT_METRICS_PATH_CAP], sha256[65];
        TpComposeCursor cursor = {value, count, 0, 1};
        tp_compose_cursor_literal(&cursor, "{\"bytes\":");
        tp_compose_cursor_u64(&cursor, &bytes);
        tp_compose_cursor_literal(&cursor, ",\"offset\":");
        tp_compose_cursor_u64(&cursor, &offset);
        tp_compose_cursor_literal(&cursor, ",\"path\":\"");
        tp_compose_cursor_text(&cursor, path, sizeof(path));
        tp_compose_cursor_literal(&cursor, ",\"sha256\":\"");
        tp_compose_cursor_text(&cursor, sha256, sizeof(sha256));
        tp_compose_cursor_literal(&cursor, "}");
        valid = cursor.valid && cursor.offset == count && tp_compose_tiling_add(tiling, path, offset, bytes, sha256);
    }
    return valid;
}


/* ------------------------------------------------------------------ state */

typedef struct TpComposeShard
{
    char const* path;
    char sha256[65];
    uint64_t bytes, records, start;
} TpComposeShard;

typedef struct TpComposeEntry
{
    char name[TP_RETIREMENT_COMPOSE_NAME_BYTES + 48];
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char sha256[65];
    uint64_t bytes;
} TpComposeEntry;

typedef struct TpComposeState
{
    TpRetirementComposeRequest const* request;
    TpRetirementStore* store;
    TpComposeFamily family;
    TpRetirementComposeBounds bounds;
    TpComposeTiling timed, untimed_tiling;
    unsigned pairs, partitions;
    TpComposeShard* shards;
    TpComposeShard* transcript;
    TpComposeShard* samples[2];
    double* ratios[TP_RETIREMENT_COMPOSE_METRICS];
    Sha256 raw;
    uint64_t invocations, untimed_records, code_records;
    uint64_t input_bytes[TP_RETIREMENT_COMPOSE_PARTITIONS];
    unsigned partition_population[TP_RETIREMENT_COMPOSE_PARTITIONS];
    unsigned partition_first[TP_RETIREMENT_COMPOSE_PARTITIONS], partition_shards[TP_RETIREMENT_COMPOSE_PARTITIONS];
    TpRetirementComposePartition const* partition_plan[TP_RETIREMENT_COMPOSE_PARTITIONS];
    char raw_sha256[65], context_sha256[65];
    char summary[512];
    TpRetirementComposeArtifact manifests[TP_RETIREMENT_COMPOSE_PARTITIONS];
    TpRetirementComposeArtifact code, series, replay, receipt, bundle, sealed, untimed;
    unsigned seal_entries;
} TpComposeState;

static TpRetirementStoredFile const* tp_compose_store_entry(TpRetirementStore const* store, char const* path)
{
    TpRetirementStoredFile const* found = NULL;
    for (unsigned i = 0; store && path && i < store->count; ++i)
        if (!strcmp(store->files[i].path, path)) found = store->files + i;
    return found;
}

static unsigned tp_compose_occurrences(char const* path, char const* const* list, unsigned count)
{
    unsigned found = 0;
    for (unsigned i = 0; list && i < count; ++i) found += list[i] && !strcmp(list[i], path);
    return found;
}

/* Every store file is exactly one declared sealed input or retained file,
 * and every declared file is in the store: no missing or extra entry. */
static int tp_compose_inventory(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    TpRetirementStore const* store = state->store;
    uint64_t declared = (uint64_t)request->transcript_count + request->sample_counts[0] + request->sample_counts[1] +
                        request->metrics_count + request->untimed_metrics_count + request->retained_count +
                        (request->untimed_path ? 1 : 0);
    int valid = declared == store->count && (!request->retained_count || request->retained_paths);
    for (unsigned i = 0; valid && i < store->count; ++i)
    {
        char const* path = store->files[i].path;
        unsigned found = tp_compose_occurrences(path, request->transcript_paths, request->transcript_count) +
            tp_compose_occurrences(path, request->sample_paths[0], request->sample_counts[0]) +
            tp_compose_occurrences(path, request->sample_paths[1], request->sample_counts[1]) +
            tp_compose_occurrences(path, request->metrics_paths, request->metrics_count) +
            tp_compose_occurrences(path, request->untimed_metrics_paths, request->untimed_metrics_count) +
            tp_compose_occurrences(path, request->retained_paths, request->retained_count) +
            (request->untimed_path && !strcmp(path, request->untimed_path));
        valid = found == 1;
    }
    return valid;
}

/* Re-read one pre-existing closure file below the evidence root without
 * following links, and require its declared size and digest. */
static int tp_compose_evidence_file(int root, char const* path, uint64_t bytes, char const* sha256)
{
    int valid = root >= 0 && tp_compose_relative_path(path) && bytes && tp_compose_digest(sha256);
    int directory = valid ? fcntl(root, F_DUPFD_CLOEXEC, 3) : -1;
    size_t length = valid ? strlen(path) : 0, start = 0;
    int fd = -1;
    valid = valid && directory >= 0;
    for (size_t i = 0; valid && i <= length; ++i)
        if (i == length || path[i] == '/')
        {
            char component[TP_RETIREMENT_STORE_PATH_BYTES + 1];
            memcpy(component, path + start, i - start);
            component[i - start] = 0;
            if (i == length) fd = openat(directory, component, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
            else
            {
                int next = openat(directory, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                close(directory);
                directory = next;
                valid = directory >= 0;
            }
            start = i + 1;
        }
    struct stat info = {0};
    valid = valid && fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
            (uint64_t)info.st_size == bytes;
    Sha256 hash;
    sha256_init(&hash);
    uint64_t used = 0;
    unsigned char buffer[65536];
    while (valid && used < bytes)
    {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        valid = count > 0 && (uint64_t)count <= bytes - used;
        if (valid)
        {
            sha256_add(&hash, buffer, (u64)count);
            used += (uint64_t)count;
        }
    }
    if (valid)
    {
        char digest[65], extra = 0;
        sha256_finish_hex(&hash, (char8*)digest);
        valid = read(fd, &extra, 1) == 0 && !strcmp(digest, sha256);
    }
    if (fd >= 0 && close(fd) != 0) valid = 0;
    if (directory >= 0 && close(directory) != 0) valid = 0;
    return valid;
}

/* Rehash the pre-sample closure and join its plan, post-A/A and result-input
 * plan entries to the identities the receipt, bundle and sealed record bind. */
static int tp_compose_prior(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    unsigned plan = 0, post = 0, result_plan = 0;
    int valid = request->prior_count && request->prior;
    for (unsigned i = 0; valid && i < request->prior_count; ++i)
    {
        TpRetirementComposeClosure const* entry = request->prior + i;
        valid = tp_compose_printable(entry->name, TP_RETIREMENT_COMPOSE_NAME_BYTES) &&
                tp_compose_evidence_file(request->evidence_root, entry->path, entry->bytes, entry->sha256);
        if (valid && !strcmp(entry->name, "workflow.execution_plan"))
            plan += !strcmp(entry->sha256, request->execution_plan_sha256);
        if (valid && !strcmp(entry->name, "workflow.phases.post_aa_binding"))
            post += !strcmp(entry->sha256, request->post_aa_binding_sha256);
        if (valid && !strcmp(entry->name, "workflow.records.result_input_plan"))
            result_plan += !strcmp(entry->sha256, request->result_input_plan_sha256);
        if (valid && (!strcmp(entry->name, "workflow.phases.sealed_result") ||
                      !strcmp(entry->name, "workflow.phases.independent_replay")))
            valid = 0;
    }
    valid = valid && plan == 1 && post == 1 && result_plan == 1;
    return valid;
}

/* One transcript line against the next #619 cursor invocation. */
static int tp_compose_invocation(TpComposeState* state, TpRetirementExecution* execution, char const* line,
                                 size_t length, uint64_t* last_end)
{
    TpRetirementComposeRequest const* request = state->request;
    TpRetirementInvocation expected = {0};
    uint64_t pid = 0, started = 0, finished = 0, sequence = 0;
    char token[129], instance[65], computed[65];
    int valid = tp_retirement_execution_peek(execution, &expected) == TP_RETIREMENT_NEXT_READY &&
        tp_compose_field_u64(line, length, "sequence", &sequence) && sequence == expected.sequence &&
        tp_compose_field_is(line, length, "kind", expected.kind ? "\"runtime\"" : "\"compiler\"") &&
        tp_compose_field_is(line, length, "phase", expected.phase ? "\"sample\"" : "\"warmup\"") &&
        tp_compose_field_is(line, length, "variant", expected.variant ? "\"candidate\"" : "\"baseline\"") &&
        tp_compose_field_optional(line, length, "group", expected.kind != 0, expected.group) &&
        tp_compose_field_optional(line, length, "row", expected.kind == 0, expected.row) &&
        tp_compose_field_optional(line, length, "round", !expected.phase, (uint64_t)(unsigned)expected.round) &&
        tp_compose_field_optional(line, length, "pair", !expected.phase, (uint64_t)(unsigned)expected.pair) &&
        tp_compose_field_optional(line, length, "position", !expected.phase, (uint64_t)(unsigned)expected.position) &&
        tp_compose_field_optional(line, length, "warmup", expected.phase != 0, (uint64_t)(unsigned)expected.warmup) &&
        tp_compose_field_is(line, length, "signal", "0") && tp_compose_field_is(line, length, "timed_out", "false") &&
        tp_compose_field_is(line, length, "cancelled", "false") &&
        tp_compose_field_u64(line, length, "pid", &pid) && pid &&
        tp_compose_field_text(line, length, "process_start_token", token, sizeof(token)) &&
        tp_compose_field_text(line, length, "process_instance_sha256", instance, sizeof(instance)) &&
        tp_retirement_process_instance(computed, request->job, request->attempt, request->boot, pid, token) &&
        !strcmp(instance, computed) &&
        tp_compose_field_u64(line, length, "started_ns", &started) &&
        tp_compose_field_u64(line, length, "finished_ns", &finished) &&
        started > *last_end && finished > started && finished < request->completed_at_ns;
    int object = valid && !expected.kind && request->layout->group_kinds[expected.group] == TP_RETIREMENT_GROUP_OBJECT;
    valid = valid && tp_compose_metrics_field(line, length, object, &state->timed);
    if (valid)
    {
        *last_end = finished;
        valid = tp_retirement_execution_commit(execution, 1);
    }
    return valid;
}

/* The complete A/B transcript: D's cursor replays the frozen seeded schedule,
 * every process instance binds the campaign identity, intervals stay ordered
 * inside the bound window, and every metrics artifact tiles its shard. */
static int tp_compose_transcript(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    TpRetirementComposeLayout const* layout = request->layout;
    TpComposeFamily const* family = &state->family;
    size_t slots = (size_t)layout->group_count * 3 + family->runtime_count;
    unsigned* workspace = (unsigned*)malloc(slots * sizeof(*workspace));
    TpRetirementExecution execution;
    int valid = workspace && request->transcript_paths && request->transcript_count &&
        request->transcript_count <= TP_RETIREMENT_TRANSCRIPT_SHARDS &&
        tp_retirement_execution_init(&execution, request->statistics->seed, layout->group_count, family->runtime_ids,
            family->runtime_count, layout->population_rows, state->pairs, workspace, slots);
    state->invocations = valid ? execution.expected : 0;
    uint64_t last_end = request->bound_at_ns;
    char* line = valid ? (char*)malloc(TP_COMPOSE_LINE_BYTES) : NULL;
    valid = valid && line;
    for (unsigned s = 0; valid && s < request->transcript_count; ++s)
    {
        char const* path = request->transcript_paths[s];
        TpComposeReader reader = {0};
        valid = path && (!s || strcmp(request->transcript_paths[s - 1], path) < 0) &&
                strcmp(path, TP_RETIREMENT_EXECUTION_RECEIPT_PATH) && tp_retirement_receipt_path(path) &&
                tp_compose_reader_open(&reader, state->store, path);
        int more = valid;
        while (valid && more)
        {
            size_t length = 0;
            int read = tp_compose_reader_line(&reader, line, TP_COMPOSE_LINE_BYTES, &length);
            if (!read) more = 0;
            else valid = read == 1 && reader.lines <= TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS &&
                         tp_compose_invocation(state, &execution, line, length, &last_end);
        }
        uint64_t records = reader.lines;
        TpRetirementStoredFile const* entry = reader.entry;
        if (valid) valid = tp_compose_reader_close(&reader);
        else tp_compose_reader_abandon(&reader);
        valid = valid && records && (s + 1 == request->transcript_count ||
                                     records == TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS);
        if (valid)
        {
            TpComposeShard* shard = state->transcript + s;
            shard->path = entry->path;
            strcpy(shard->sha256, entry->sha256);
            shard->bytes = entry->bytes;
            shard->records = records;
        }
    }
    valid = valid && tp_retirement_execution_complete(&execution) && execution.sequence == state->invocations &&
            tp_compose_tiling_finish(&state->timed);
    free(line);
    free(workspace);
    return valid;
}

/* Untimed code-artifact batch records: strictly ordered (group, variant,
 * purpose), each a fresh campaign-bound process outside the timed window,
 * one reproduction per group and variant, metrics tiling the untimed shards. */
static int tp_compose_untimed(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    unsigned groups = request->layout->untimed_groups;
    int valid = groups ? request->untimed_path != NULL : !request->untimed_path && !request->untimed_metrics_count;
    unsigned char* reproduced = valid && groups ? (unsigned char*)calloc((size_t)groups * 2, 1) : NULL;
    char* line = valid && groups ? (char*)malloc(TP_COMPOSE_LINE_BYTES) : NULL;
    valid = valid && (!groups || (reproduced && line));
    TpComposeReader reader = {0};
    if (valid && groups) valid = tp_compose_reader_open(&reader, state->store, request->untimed_path);
    uint64_t last_end = 0, last_key = UINT64_MAX;
    int more = valid && groups;
    while (valid && more)
    {
        size_t length = 0;
        int read = tp_compose_reader_line(&reader, line, TP_COMPOSE_LINE_BYTES, &length);
        uint64_t group = 0, exit_status = 0, pid = 0, started = 0, finished = 0;
        char token[129], instance[65], computed[65], digest[65];
        if (!read) more = 0;
        else
        {
            int baseline = read == 1 && tp_compose_field_is(line, length, "variant", "\"baseline\"");
            int candidate = read == 1 && tp_compose_field_is(line, length, "variant", "\"candidate\"");
            int production = read == 1 && tp_compose_field_is(line, length, "purpose", "\"production\"");
            int reproduction = read == 1 && tp_compose_field_is(line, length, "purpose", "\"reproduction\"");
            valid = read == 1 && reader.lines <= (uint64_t)groups * 4 &&
                tp_compose_field_u64(line, length, "group", &group) && group < groups &&
                baseline + candidate == 1 && production + reproduction == 1 &&
                tp_compose_field_u64(line, length, "exit_status", &exit_status) && exit_status <= 255 &&
                tp_compose_field_text(line, length, "command_sha256", digest, sizeof(digest)) &&
                tp_compose_digest(digest) &&
                tp_compose_field_text(line, length, "executable_sha256", digest, sizeof(digest)) &&
                tp_compose_digest(digest) &&
                tp_compose_field_text(line, length, "output_sha256", digest, sizeof(digest)) &&
                tp_compose_digest(digest) &&
                tp_compose_field_u64(line, length, "pid", &pid) && pid &&
                tp_compose_field_text(line, length, "process_start_token", token, sizeof(token)) &&
                tp_compose_field_text(line, length, "process_instance_sha256", instance, sizeof(instance)) &&
                tp_retirement_process_instance(computed, request->job, request->attempt, request->boot, pid, token) &&
                !strcmp(instance, computed) &&
                tp_compose_field_u64(line, length, "started_ns", &started) &&
                tp_compose_field_u64(line, length, "finished_ns", &finished) &&
                finished > started && started > last_end &&
                (finished < request->bound_at_ns || started > request->completed_at_ns) &&
                tp_compose_metrics_field(line, length, -1, &state->untimed_tiling);
            uint64_t key = (group * 2 + (uint64_t)candidate) * 2 + (uint64_t)reproduction;
            valid = valid && (last_key == UINT64_MAX || key > last_key);
            if (valid)
            {
                last_key = key;
                last_end = finished;
                if (reproduction) reproduced[group * 2 + (uint64_t)candidate] = 1;
            }
        }
    }
    if (groups)
    {
        uint64_t records = reader.lines;
        TpRetirementStoredFile const* entry = reader.entry;
        if (valid) valid = tp_compose_reader_close(&reader);
        else tp_compose_reader_abandon(&reader);
        for (unsigned i = 0; valid && i < groups * 2; ++i) valid = reproduced[i] == 1;
        valid = valid && records;
        if (valid)
        {
            strcpy(state->untimed.path, entry->path);
            strcpy(state->untimed.sha256, entry->sha256);
            state->untimed.bytes = entry->bytes;
            state->untimed_records = records;
        }
    }
    valid = valid && tp_compose_tiling_finish(&state->untimed_tiling);
    free(line);
    free(reproduced);
    return valid;
}

/* A candidate/baseline ratio exactly as the validator recomputes it. */
static int tp_compose_ratio(double candidate, double baseline, double* output)
{
    double ratio = candidate / baseline;
    int valid = isfinite(ratio) && ratio > 0.0;
    *output = valid ? ratio : 0.0;
    return valid;
}

/* One `row-round-pair` record of D's tp_retirement_sample_record, at its
 * frozen coordinate, capturing its wall, memory and runtime ratios. */
static int tp_compose_row_record(TpComposeState* state, char const* line, size_t length, uint64_t ordinal)
{
    unsigned pairs = state->pairs;
    uint64_t per_unit = (uint64_t)TP_RETIREMENT_ROUNDS * pairs;
    unsigned unit = (unsigned)(ordinal / per_unit), round = (unsigned)(ordinal / pairs % TP_RETIREMENT_ROUNDS);
    unsigned pair = (unsigned)(ordinal % pairs);
    TpRetirementComposeRow const* row = state->request->layout->rows + unit;
    double memory[2], wall[2], runtime[2] = {1.0, 1.0};
    uint64_t pair_field = 0, id = 0, round_id = 0, pair_id = 0, round_field = 0, row_field = 0;
    TpComposeCursor cursor = {line, length, 0, 1};
    tp_compose_cursor_literal(&cursor, "{\"measurements\":{\"compiler_peak_memory\":{\"baseline\":");
    tp_compose_cursor_number(&cursor, &memory[0]);
    tp_compose_cursor_literal(&cursor, ",\"candidate\":");
    tp_compose_cursor_number(&cursor, &memory[1]);
    tp_compose_cursor_literal(&cursor, "},\"compiler_wall_time\":{\"baseline\":");
    tp_compose_cursor_number(&cursor, &wall[0]);
    tp_compose_cursor_literal(&cursor, ",\"candidate\":");
    tp_compose_cursor_number(&cursor, &wall[1]);
    tp_compose_cursor_literal(&cursor, "}");
    if (row->runtime)
    {
        tp_compose_cursor_literal(&cursor, ",\"generated_runtime\":{\"baseline\":");
        tp_compose_cursor_number(&cursor, &runtime[0]);
        tp_compose_cursor_literal(&cursor, ",\"candidate\":");
        tp_compose_cursor_number(&cursor, &runtime[1]);
        tp_compose_cursor_literal(&cursor, "}");
    }
    tp_compose_cursor_literal(&cursor, "},\"pair\":");
    tp_compose_cursor_u64(&cursor, &pair_field);
    tp_compose_cursor_literal(&cursor, ",\"record_id\":\"row-");
    tp_compose_cursor_u64(&cursor, &id);
    tp_compose_cursor_literal(&cursor, "/round-");
    tp_compose_cursor_u64(&cursor, &round_id);
    tp_compose_cursor_literal(&cursor, "/pair-");
    tp_compose_cursor_u64(&cursor, &pair_id);
    tp_compose_cursor_literal(&cursor, "\",\"round\":");
    tp_compose_cursor_u64(&cursor, &round_field);
    tp_compose_cursor_literal(&cursor, ",\"row\":");
    tp_compose_cursor_u64(&cursor, &row_field);
    tp_compose_cursor_literal(&cursor, "}\n");
    size_t index = (size_t)unit * per_unit + (size_t)round * pairs + pair;
    int valid = cursor.valid && cursor.offset == length && pair_field == pair && pair_id == pair &&
                round_id == round && round_field == round && id == row->id && row_field == row->id &&
                tp_compose_ratio(wall[1], wall[0], &state->ratios[0][index]) &&
                tp_compose_ratio(memory[1], memory[0], &state->ratios[1][index]);
    if (valid && row->runtime)
        valid = tp_compose_ratio(runtime[1], runtime[0], &state->ratios[2][(size_t)state->family.runtime_index[unit] *
            per_unit + (size_t)round * pairs + pair]);
    return valid;
}

/* One `group-round-pair` record of D's tp_retirement_batch_record. */
static int tp_compose_batch_record(TpComposeState* state, char const* line, size_t length, uint64_t ordinal)
{
    unsigned pairs = state->pairs;
    uint64_t per_unit = (uint64_t)TP_RETIREMENT_ROUNDS * pairs;
    unsigned unit = (unsigned)(ordinal / per_unit), round = (unsigned)(ordinal / pairs % TP_RETIREMENT_ROUNDS);
    unsigned pair = (unsigned)(ordinal % pairs);
    unsigned group = state->family.object_groups[unit];
    double rss[2], wall[2];
    uint64_t group_field = 0, pair_field = 0, group_id = 0, round_id = 0, pair_id = 0, round_field = 0;
    TpComposeCursor cursor = {line, length, 0, 1};
    tp_compose_cursor_literal(&cursor, "{\"group\":");
    tp_compose_cursor_u64(&cursor, &group_field);
    tp_compose_cursor_literal(&cursor, ",\"measurements\":{\"compiler_batch_peak_rss\":{\"baseline\":");
    tp_compose_cursor_number(&cursor, &rss[0]);
    tp_compose_cursor_literal(&cursor, ",\"candidate\":");
    tp_compose_cursor_number(&cursor, &rss[1]);
    tp_compose_cursor_literal(&cursor, "},\"compiler_batch_wall_time\":{\"baseline\":");
    tp_compose_cursor_number(&cursor, &wall[0]);
    tp_compose_cursor_literal(&cursor, ",\"candidate\":");
    tp_compose_cursor_number(&cursor, &wall[1]);
    tp_compose_cursor_literal(&cursor, "}},\"pair\":");
    tp_compose_cursor_u64(&cursor, &pair_field);
    tp_compose_cursor_literal(&cursor, ",\"record_id\":\"group-");
    tp_compose_cursor_u64(&cursor, &group_id);
    tp_compose_cursor_literal(&cursor, "/round-");
    tp_compose_cursor_u64(&cursor, &round_id);
    tp_compose_cursor_literal(&cursor, "/pair-");
    tp_compose_cursor_u64(&cursor, &pair_id);
    tp_compose_cursor_literal(&cursor, "\",\"round\":");
    tp_compose_cursor_u64(&cursor, &round_field);
    tp_compose_cursor_literal(&cursor, "}\n");
    size_t index = (size_t)unit * per_unit + (size_t)round * pairs + pair;
    int valid = cursor.valid && cursor.offset == length && group_field == group && group_id == group &&
                pair_field == pair && pair_id == pair && round_id == round && round_field == round &&
                tp_compose_ratio(wall[1], wall[0], &state->ratios[3][index]) &&
                tp_compose_ratio(rss[1], rss[0], &state->ratios[4][index]);
    return valid;
}

/* Both #615 populations, in canonical record order: every record present
 * once at its frozen coordinate, full-size shards before the last, and the
 * streamed bytes (rows, then batches) are the raw measurement digest. */
static int tp_compose_samples(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    uint64_t per_unit = (uint64_t)TP_RETIREMENT_ROUNDS * state->pairs;
    uint64_t expected[2] = {(uint64_t)request->layout->row_count * per_unit,
                            (uint64_t)state->family.object_count * per_unit};
    char* line = (char*)malloc(TP_RETIREMENT_SAMPLE_LINE_CAP);
    int valid = line != NULL;
    sha256_init(&state->raw);
    for (unsigned p = 0; valid && p < 2; ++p)
    {
        unsigned count = request->sample_counts[p];
        uint64_t ordinal = 0;
        valid = count && request->sample_paths[p] && count <= TP_RETIREMENT_TRANSCRIPT_SHARDS;
        for (unsigned s = 0; valid && s < count; ++s)
        {
            TpComposeReader reader = {0};
            uint64_t start = ordinal;
            valid = request->sample_paths[p][s] && tp_compose_reader_open(&reader, state->store, request->sample_paths[p][s]);
            int more = valid;
            while (valid && more)
            {
                size_t length = 0;
                int read = tp_compose_reader_line(&reader, line, TP_RETIREMENT_SAMPLE_LINE_CAP, &length);
                if (!read) more = 0;
                else
                {
                    valid = read == 1 && ordinal < expected[p] &&
                            (p ? tp_compose_batch_record(state, line, length, ordinal) :
                                 tp_compose_row_record(state, line, length, ordinal));
                    if (valid)
                    {
                        sha256_add(&state->raw, line, (u64)length);
                        ++ordinal;
                    }
                }
            }
            uint64_t records = reader.lines;
            TpRetirementStoredFile const* entry = reader.entry;
            if (valid) valid = tp_compose_reader_close(&reader);
            else tp_compose_reader_abandon(&reader);
            valid = valid && records && (s + 1 == count || records == TP_RETIREMENT_SAMPLE_SHARD_RECORDS);
            if (valid)
            {
                TpComposeShard* shard = state->samples[p] + s;
                shard->path = entry->path;
                strcpy(shard->sha256, entry->sha256);
                shard->bytes = entry->bytes;
                shard->records = records;
                shard->start = start;
            }
        }
        valid = valid && ordinal == expected[p];
    }
    if (valid) sha256_finish_hex(&state->raw, (char8*)state->raw_sha256);
    free(line);
    return valid;
}

/* The pre-sample plan's partitions: contiguous full-cap #615 partitions per
 * population, unique identities and paths, and whole shards in each. */
static int tp_compose_partitions(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    uint64_t per_unit = (uint64_t)TP_RETIREMENT_ROUNDS * state->pairs;
    uint64_t expected[2] = {(uint64_t)request->layout->row_count * per_unit,
                            (uint64_t)state->family.object_count * per_unit};
    unsigned total = request->partition_counts[0] + request->partition_counts[1];
    int valid = request->partition_counts[0] && request->partition_counts[1] &&
                total <= TP_RETIREMENT_COMPOSE_PARTITIONS && total == state->bounds.manifest_count &&
                request->partitions[0] && request->partitions[1];
    unsigned global = 0;
    for (unsigned p = 0; valid && p < 2; ++p)
    {
        uint64_t start = 0;
        unsigned shard = 0;
        for (unsigned i = 0; valid && i < request->partition_counts[p]; ++i)
        {
            TpRetirementComposePartition const* partition = request->partitions[p] + i;
            uint64_t records = i + 1 < request->partition_counts[p] ? TP_RETIREMENT_SAMPLE_PARTITION_RECORDS :
                               expected[p] - start;
            valid = tp_retirement_token(partition->identity) && tp_retirement_receipt_path(partition->path) &&
                    partition->start == start && partition->records == records && records &&
                    records <= TP_RETIREMENT_SAMPLE_PARTITION_RECORDS;
            for (unsigned j = 0; valid && j < global; ++j)
                valid = strcmp(state->partition_plan[j]->identity, partition->identity) &&
                        strcmp(state->partition_plan[j]->path, partition->path);
            unsigned first = shard;
            while (valid && shard < request->sample_counts[p] &&
                   state->samples[p][shard].start < start + records)
            {
                valid = state->samples[p][shard].start + state->samples[p][shard].records <= start + records;
                ++shard;
            }
            valid = valid && shard > first;
            if (valid)
            {
                state->partition_plan[global] = partition;
                state->partition_population[global] = p;
                state->partition_first[global] = first;
                state->partition_shards[global] = shard - first;
                ++global;
                start += records;
            }
        }
        valid = valid && start == expected[p] && shard == request->sample_counts[p];
    }
    state->partitions = valid ? global : 0;
    return valid;
}

/* The post-sample execution context: the frozen canonical template with the
 * streamed numeric digest substituted for its placeholder. */
static int tp_compose_context(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    unsigned char const* template = request->context_template;
    size_t length = request->context_template_bytes;
    size_t placeholder = strlen(TP_COMPOSE_PLACEHOLDER);
    char post[128];
    int count = snprintf(post, sizeof(post), "\"post_aa_binding_sha256\":\"%s\"", request->post_aa_binding_sha256);
    int valid = template && length > placeholder && length <= TP_COMPOSE_TEMPLATE_BYTES && template[0] == '{' &&
                template[length - 1] == '}' && !memchr(template, '\n', length) && !memchr(template, 0, length) &&
                count > 0 && (size_t)count < sizeof(post) && memmem(template, length, post, (size_t)count);
    unsigned occurrences = 0;
    size_t at = 0;
    for (size_t i = 0; valid && i + placeholder <= length; ++i)
        if (!memcmp(template + i, TP_COMPOSE_PLACEHOLDER, placeholder))
        {
            ++occurrences;
            at = i;
        }
    valid = valid && occurrences == 1;
    unsigned char* copy = valid ? (unsigned char*)malloc(length) : NULL;
    valid = valid && copy;
    if (valid)
    {
        memcpy(copy, template, length);
        memcpy(copy + at + placeholder - 65, state->raw_sha256, 64);
        tp_compose_sha_hex(copy, length, state->context_sha256);
    }
    free(copy);
    return valid;
}

/* ----------------------------------------------------------------- writers */

/* One composer output: bytes go to a pending store file (and an optional
 * identical private copy) while being hashed; publish seals it. */
typedef struct TpComposeWriter
{
    TpRetirementStore* store;
    TpRetirementPending pending;
    FILE* copy;
    Sha256 hash;
    uint64_t bytes, limit;
    int valid;
} TpComposeWriter;

static void tp_compose_writer_begin(TpComposeWriter* writer, TpRetirementStore* store, char const* path,
                                    uint64_t limit, FILE* copy)
{
    *writer = (TpComposeWriter){.store = store, .copy = copy, .limit = limit};
    sha256_init(&writer->hash);
    writer->valid = limit && limit <= TP_RETIREMENT_STORE_FILE_BYTES &&
                    tp_retirement_store_begin(store, path, limit, &writer->pending);
}

static void tp_compose_writer_bytes(TpComposeWriter* writer, void const* data, size_t length)
{
    writer->valid = writer->valid && length <= writer->limit - writer->bytes &&
                    fwrite(data, 1, length, writer->pending.stream) == length &&
                    (!writer->copy || fwrite(data, 1, length, writer->copy) == length);
    if (writer->valid)
    {
        sha256_add(&writer->hash, data, (u64)length);
        writer->bytes += length;
    }
}

static void tp_compose_writer_format(TpComposeWriter* writer, char const* format, ...)
{
    char buffer[2048];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    writer->valid = writer->valid && length >= 0 && (size_t)length < sizeof(buffer);
    if (writer->valid) tp_compose_writer_bytes(writer, buffer, (size_t)length);
}

static int tp_compose_writer_publish(TpComposeWriter* writer, TpRetirementComposeArtifact* artifact)
{
    char digest[65];
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    int valid = writer->valid && writer->bytes && (!writer->copy || (fflush(writer->copy) == 0 && !ferror(writer->copy)));
    if (valid)
    {
        sha256_finish_hex(&writer->hash, (char8*)digest);
        strcpy(path, writer->pending.path);
        valid = tp_retirement_store_publish(writer->store, &writer->pending, writer->bytes, digest);
        if (valid && artifact)
        {
            strcpy(artifact->path, path);
            strcpy(artifact->sha256, digest);
            artifact->bytes = writer->bytes;
        }
    }
    else if (writer->pending.stream) tp_retirement_store_abort(writer->store, &writer->pending);
    writer->valid = valid;
    return valid;
}

/* ----------------------------------------------------------------- outputs */

/* One #615 manifest per predeclared partition, listing its whole shards. */
static int tp_compose_manifests(TpComposeState* state)
{
    int valid = 1;
    for (unsigned g = 0; valid && g < state->partitions; ++g)
    {
        unsigned population = state->partition_population[g];
        TpComposeShard const* shards = state->samples[population];
        TpComposeWriter writer;
        uint64_t limit = TP_COMPOSE_MANIFEST_FIXED_BYTES + (uint64_t)state->partition_shards[g] * TP_COMPOSE_MANIFEST_SHARD_BYTES;
        uint64_t inputs = 0;
        tp_compose_writer_begin(&writer, state->store, state->partition_plan[g]->path, limit, NULL);
        tp_compose_writer_format(&writer, "{\"identity_field\":\"record_id\","
                                          "\"schema\":\"buster-streaming-evidence-shards-v1\",\"shards\":[");
        for (unsigned s = 0; s < state->partition_shards[g]; ++s)
        {
            unsigned index = state->partition_first[g] + s;
            tp_compose_writer_format(&writer,
                "%s{\"bytes\":%" PRIu64 ",\"identity\":\"%s-%04u\",\"path\":\"%s\",\"sha256\":\"%s\"}",
                s ? "," : "", shards[index].bytes, population ? "batches" : "samples", index, shards[index].path,
                shards[index].sha256);
            inputs += shards[index].bytes;
        }
        tp_compose_writer_format(&writer, "],\"version\":1}\n");
        valid = tp_compose_writer_publish(&writer, state->manifests + g);
        if (valid) state->input_bytes[g] = state->manifests[g].bytes + inputs;
    }
    return valid;
}

/* candidate * 100 <= baseline * 101 without widening: for candidate above
 * baseline this is 100 * (candidate - baseline) <= baseline. */
static int tp_compose_within_one_percent(uint64_t candidate, uint64_t baseline)
{
    int within = candidate <= baseline || candidate - baseline <= baseline / 100;
    return within;
}

/* The code-byte record set through D's encoder, and the exact summary the
 * validator's _code_bytes_summary derives from it. */
static int tp_compose_code(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    TpRetirementPending pending = {0};
    TpRetirementCodeRecords records;
    TpRetirementShard descriptor = {0};
    int valid = request->code && request->code_count &&
                tp_retirement_store_begin(state->store, TP_RETIREMENT_COMPOSE_CODE_PATH, state->bounds.code, &pending) &&
                tp_retirement_code_records_init(&records, pending.stream);
    uint64_t baseline_total = 0, candidate_total = 0, rows = 0;
    double maximum = 0.0;
    int cell_pass = 1;
    Sha256 ratios;
    sha256_init(&ratios);
    sha256_add(&ratios, "[", 1);
    for (unsigned i = 0; valid && i < request->code_count; ++i)
    {
        TpRetirementComposeCode const* code = request->code + i;
        uint64_t baseline = code->sides[0].code_bytes, candidate = code->sides[1].code_bytes;
        valid = baseline <= TP_COMPOSE_CODE_BYTES_MAX && candidate <= TP_COMPOSE_CODE_BYTES_MAX &&
                tp_retirement_code_records_append(&records, code->row, code->sides);
        if (valid && baseline)
        {
            char text[40], item[96];
            double ratio = (double)candidate / (double)baseline;
            valid = tp_compose_add(&baseline_total, baseline) && tp_compose_add(&candidate_total, candidate) &&
                    baseline_total <= TP_COMPOSE_TOTAL_BYTES_MAX && candidate_total <= TP_COMPOSE_TOTAL_BYTES_MAX &&
                    tp_compose_repr(ratio, text);
            int count = valid ? snprintf(item, sizeof(item), "%s{\"ratio\":%s,\"row\":%u}", rows ? "," : "", text,
                                         code->row) : -1;
            valid = valid && count > 0 && (size_t)count < sizeof(item);
            if (valid)
            {
                sha256_add(&ratios, item, (u64)count);
                cell_pass = cell_pass && tp_compose_within_one_percent(candidate, baseline);
                if (!rows || ratio > maximum) maximum = ratio;
                ++rows;
            }
        }
    }
    valid = valid && rows && tp_retirement_code_records_finish(&records, &descriptor) &&
            tp_retirement_store_publish(state->store, &pending, descriptor.bytes, descriptor.sha256);
    if (!valid && pending.stream) tp_retirement_store_abort(state->store, &pending);
    double aggregate = 0.0;
    char aggregate_text[40], maximum_text[40], ratios_sha256[65];
    valid = valid && tp_compose_decimal_ratio(candidate_total, baseline_total, &aggregate) &&
            tp_compose_repr(aggregate, aggregate_text) && tp_compose_repr(maximum, maximum_text);
    if (valid)
    {
        sha256_add(&ratios, "]", 1);
        sha256_finish_hex(&ratios, (char8*)ratios_sha256);
        int aggregate_pass = tp_compose_within_one_percent(candidate_total, baseline_total);
        int count = snprintf(state->summary, sizeof(state->summary),
            "{\"aggregate_pass\":%s,\"aggregate_ratio\":%s,\"per_cell_max_ratio\":%s,\"per_cell_pass\":%s,"
            "\"ratios_sha256\":\"%s\",\"rows\":%" PRIu64 "}", aggregate_pass ? "true" : "false", aggregate_text,
            maximum_text, cell_pass ? "true" : "false", ratios_sha256, rows);
        valid = count > 0 && (size_t)count < sizeof(state->summary);
        strcpy(state->code.path, TP_RETIREMENT_COMPOSE_CODE_PATH);
        strcpy(state->code.sha256, descriptor.sha256);
        state->code.bytes = descriptor.bytes;
        state->code_records = descriptor.records;
    }
    return valid;
}

/* The #619 statistics input the reviewed adapter consumes: every family
 * member in sorted order with its cells' ratios, round-major. The same bytes
 * go to the store and to the adapter's private scratch copy. */
static int tp_compose_series(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    TpComposeFamily const* family = &state->family;
    TpRetirementPlan const* plan = request->statistics;
    uint64_t per_unit = (uint64_t)TP_RETIREMENT_ROUNDS * state->pairs;
    int fd = openat(request->scratch_root, TP_RETIREMENT_COMPOSE_SERIES_PATH,
                    O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
    FILE* copy = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && !copy) close(fd);
    TpComposeWriter writer;
    tp_compose_writer_begin(&writer, state->store, TP_RETIREMENT_COMPOSE_SERIES_PATH, state->bounds.series, copy);
    writer.valid = writer.valid && copy;
    tp_compose_writer_format(&writer, "version=%u seed=%" PRIu64 " bootstrap_members=%u cell_members=%u pairs=%u "
        "resamples=%u frozen=1 members=%u\n", plan->version, plan->seed, family->bootstrap, family->cells_total,
        state->pairs, plan->resamples, family->count);
    for (unsigned i = 0; writer.valid && i < family->count; ++i)
    {
        TpComposeMember const* member = family->members + family->order[i];
        tp_compose_writer_format(&writer, "member=%s metric=%u kind=%u family=%u cells=%u pairs=%u resamples=%u limit=%s\n",
            member->name, member->metric, member->kind, member->family, member->cells, state->pairs,
            member->kind ? 0u : plan->resamples,
            member->kind ? tp_compose_cell_limits[member->metric] : tp_compose_aggregate_limits[member->metric]);
        unsigned written = 0;
        for (unsigned cell = 0; writer.valid && cell < family->cells[member->metric]; ++cell)
        {
            int selected = member->kind ? cell == member->unit : member->dimension == TP_COMPOSE_NONE ||
                !strcmp(tp_compose_cell_value(family, member->metric, cell, member->dimension), member->value);
            for (uint64_t index = 0; selected && writer.valid && index < per_unit; ++index)
                tp_compose_writer_format(&writer, "ratio=%.17g\n", state->ratios[member->metric][cell * per_unit + index]);
            written += (unsigned)selected;
        }
        writer.valid = writer.valid && written == member->cells;
        tp_compose_writer_format(&writer, "end\n");
    }
    int valid = tp_compose_writer_publish(&writer, &state->series);
    if (copy && fclose(copy) != 0) valid = 0;
    return valid;
}

/* The adapter's own JSON: the approved schema, every family member once in
 * the series order, each result valid. Its numbers are replayed by the
 * validator from the reviewed source, never trusted here. */
static int tp_compose_replay_check(TpComposeState* state, char const* bytes, size_t length)
{
    static char const prefix[] = "{\"schema\":\"buster-native-retirement-statistics-replay-v1\",\"version\":1,\"members\":[";
    static char const opening[] = "{\"member\":\"";
    TpComposeFamily const* family = &state->family;
    int valid = bytes && length > sizeof(prefix) + 3 && !memcmp(bytes, prefix, sizeof(prefix) - 1) &&
                !memcmp(bytes + length - 3, "]}\n", 3) && !memchr(bytes, 0, length) &&
                !memmem(bytes, length, "\"valid\":false", 13);
    size_t offset = sizeof(prefix) - 1;
    for (unsigned i = 0; valid && i < family->count; ++i)
    {
        char const* name = family->members[family->order[i]].name;
        char expected[TP_RETIREMENT_COMPOSE_MEMBER_BYTES + 16];
        int count = snprintf(expected, sizeof(expected), "%s%s%s\",", i ? "," : "", opening, name);
        char const* found = (char const*)memmem(bytes + offset, length - offset, opening, sizeof(opening) - 1);
        size_t at = found ? (size_t)(found - bytes) - (i ? 1 : 0) : 0;
        valid = found && count > 0 && at >= offset && (i || at == offset) && at + (size_t)count <= length &&
                !memcmp(bytes + at, expected, (size_t)count) &&
                memmem(bytes + at, length - at, "\"valid\":true", 12);
        if (valid) offset = at + (size_t)count;
    }
    valid = valid && !memmem(bytes + offset, length - offset, opening, sizeof(opening) - 1);
    return valid;
}

/* Run the reviewed `bench_throughput retirement-replay` adapter on the
 * scratch copy of the series, then seal its output unchanged. */
static int tp_compose_adapter(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    struct stat info = {0};
    int valid = request->adapter_path && request->adapter_path[0] == '/' && request->scratch_root >= 0 &&
                fstatat(request->scratch_root, TP_RETIREMENT_COMPOSE_REPLAY_PATH, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                errno == ENOENT;
    pid_t child = valid ? fork() : -1;
    if (child == 0)
    {
        char* const arguments[] = {(char*)request->adapter_path, (char*)"retirement-replay", (char*)"--input",
                                   (char*)TP_RETIREMENT_COMPOSE_SERIES_PATH, (char*)"--output",
                                   (char*)TP_RETIREMENT_COMPOSE_REPLAY_PATH, NULL};
        char* const environment[] = {(char*)"LC_ALL=C", NULL};
        if (fchdir(request->scratch_root) == 0)
        {
#ifdef SYS_close_range
            syscall(SYS_close_range, 3u, ~0u, 0u);
#endif
            execve(request->adapter_path, arguments, environment);
        }
        _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    while (child > 0 && waited < 0)
    {
        waited = waitpid(child, &status, 0);
        if (waited < 0 && errno != EINTR) break;
    }
    valid = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    int fd = valid ? openat(request->scratch_root, TP_RETIREMENT_COMPOSE_REPLAY_PATH,
                            O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
    valid = valid && fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
            (uint64_t)info.st_size <= state->bounds.replay;
    size_t length = valid ? (size_t)info.st_size : 0, used = 0;
    char* bytes = valid ? (char*)malloc(length + 1) : NULL;
    valid = valid && bytes;
    while (valid && used < length)
    {
        ssize_t count = read(fd, bytes + used, length - used);
        if (count < 0 && errno == EINTR) continue;
        valid = count > 0;
        if (valid) used += (size_t)count;
    }
    if (fd >= 0 && close(fd) != 0) valid = 0;
    valid = valid && tp_compose_replay_check(state, bytes, length);
    TpComposeWriter writer;
    if (valid)
    {
        tp_compose_writer_begin(&writer, state->store, TP_RETIREMENT_COMPOSE_REPLAY_PATH, state->bounds.replay, NULL);
        tp_compose_writer_bytes(&writer, bytes, length);
        valid = tp_compose_writer_publish(&writer, &state->replay);
    }
    free(bytes);
    if (request->scratch_root >= 0)
    {
        unlinkat(request->scratch_root, TP_RETIREMENT_COMPOSE_REPLAY_PATH, 0);
        unlinkat(request->scratch_root, TP_RETIREMENT_COMPOSE_SERIES_PATH, 0);
    }
    return valid;
}

/* The post-sample execution receipt in D's canonical encoding
 * (tp_retirement_transcript_receipt), for the composed context. */
static int tp_compose_receipt(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    TpComposeWriter writer;
    tp_compose_writer_begin(&writer, state->store, TP_RETIREMENT_EXECUTION_RECEIPT_PATH, TP_RETIREMENT_RECEIPT_BYTES, NULL);
    tp_compose_writer_format(&writer, "{\"attempt\":%" PRIu64 ",\"boot_id\":\"%s\",\"bound_at_ns\":%" PRIu64
        ",\"completed_at_ns\":%" PRIu64 ",\"context_sha256\":\"%s\",\"execution_plan_sha256\":\"%s\""
        ",\"invocations\":%" PRIu64 ",\"job_id\":\"%s\",\"schema\":\"buster-native-retirement-execution-receipt-v1\""
        ",\"shards\":[", request->attempt, request->boot, request->bound_at_ns, request->completed_at_ns,
        state->context_sha256, request->execution_plan_sha256, state->invocations, request->job);
    for (unsigned s = 0; s < request->transcript_count; ++s)
    {
        TpComposeShard const* shard = state->transcript + s;
        tp_compose_writer_format(&writer, "%s{\"bytes\":%" PRIu64 ",\"path\":\"%s\",\"records\":%" PRIu64 ",\"sha256\":\"%s\"}",
            s ? "," : "", shard->bytes, shard->path, shard->records, shard->sha256);
    }
    tp_compose_writer_format(&writer, "],\"version\":1}\n");
    int valid = tp_compose_writer_publish(&writer, &state->receipt);
    return valid;
}

static void tp_compose_descriptor(TpComposeWriter* writer, TpRetirementComposeArtifact const* artifact)
{
    tp_compose_writer_format(writer, "{\"bytes\":%" PRIu64 ",\"path\":\"%s\",\"sha256\":\"%s\"}", artifact->bytes,
                             artifact->path, artifact->sha256);
}

static void tp_compose_manifest_list(TpComposeWriter* writer, TpComposeState const* state, unsigned population)
{
    unsigned written = 0;
    tp_compose_writer_format(writer, "[");
    for (unsigned g = 0; g < state->partitions; ++g)
        if (state->partition_population[g] == population)
        {
            TpRetirementComposePartition const* plan = state->partition_plan[g];
            tp_compose_writer_format(writer, "%s{\"bytes\":%" PRIu64 ",\"identity\":\"%s\",\"input_bytes\":%" PRIu64
                ",\"path\":\"%s\",\"records\":%" PRIu64 ",\"sha256\":\"%s\",\"start_record\":%" PRIu64 "}",
                written++ ? "," : "", state->manifests[g].bytes, plan->identity, state->input_bytes[g],
                state->manifests[g].path, plan->records, state->manifests[g].sha256, plan->start);
        }
    tp_compose_writer_format(writer, "]");
}

/* The member-invocation digest: one #619 call per member, all three scopes. */
static void tp_compose_invocations_sha256(TpComposeFamily const* family, char output[65])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, "[", 1);
    for (unsigned i = 0; i < family->count; ++i)
    {
        char item[TP_RETIREMENT_COMPOSE_MEMBER_BYTES + 64];
        int count = snprintf(item, sizeof(item), "%s{\"member\":\"%s\",\"scopes\":[\"round-1\",\"round-2\",\"pooled\"]}",
                             i ? "," : "", family->members[family->order[i]].name);
        sha256_add(&hash, item, (u64)count);
    }
    sha256_add(&hash, "]", 1);
    sha256_finish_hex(&hash, (char8*)output);
}

/* The existing-schema result bundle (buster-native-retirement-result-bundle-v2). */
static int tp_compose_bundle(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    char invocations[65];
    tp_compose_invocations_sha256(&state->family, invocations);
    TpComposeWriter writer;
    tp_compose_writer_begin(&writer, state->store, TP_RETIREMENT_COMPOSE_BUNDLE_PATH, state->bounds.bundle, NULL);
    tp_compose_writer_format(&writer, "{\"adapter_input\":");
    tp_compose_descriptor(&writer, &state->series);
    tp_compose_writer_format(&writer, ",\"batch_result_manifests\":");
    tp_compose_manifest_list(&writer, state, 1);
    tp_compose_writer_format(&writer, ",\"code_bytes_summary\":%s,\"code_records\":{\"bytes\":%" PRIu64
        ",\"path\":\"%s\",\"records\":%" PRIu64 ",\"sha256\":\"%s\"},\"execution_receipt\":", state->summary,
        state->code.bytes, state->code.path, state->code_records, state->code.sha256);
    tp_compose_descriptor(&writer, &state->receipt);
    tp_compose_writer_format(&writer, ",\"family_sha256\":\"%s\",\"member_count\":%u,\"member_invocations_sha256\":\"%s\""
        ",\"raw_measurements_sha256\":\"%s\",\"result_input_plan_sha256\":\"%s\",\"result_manifests\":",
        request->family_sha256, state->family.count, invocations, state->raw_sha256, request->result_input_plan_sha256);
    tp_compose_manifest_list(&writer, state, 0);
    tp_compose_writer_format(&writer, ",\"schema\":\"buster-native-retirement-result-bundle-v2\",\"scopes_per_member\":3,"
        "\"source_rows_sha256\":\"%s\",\"untimed_batches\":", request->source_rows_sha256);
    if (request->layout->untimed_groups)
        tp_compose_writer_format(&writer, "{\"bytes\":%" PRIu64 ",\"path\":\"%s\",\"records\":%" PRIu64 ",\"sha256\":\"%s\"}",
            state->untimed.bytes, state->untimed.path, state->untimed_records, state->untimed.sha256);
    else tp_compose_writer_format(&writer, "null");
    tp_compose_writer_format(&writer, ",\"version\":1}\n");
    int valid = tp_compose_writer_publish(&writer, &state->bundle);
    return valid;
}

static int tp_compose_entry(TpComposeEntry* entries, unsigned* count, unsigned capacity, char const* path,
                            uint64_t bytes, char const* sha256, char const* format, ...)
{
    int valid = *count < capacity && tp_compose_relative_path(path) && bytes && tp_compose_digest(sha256);
    if (valid)
    {
        TpComposeEntry* entry = entries + *count;
        va_list arguments;
        va_start(arguments, format);
        int length = vsnprintf(entry->name, sizeof(entry->name), format, arguments);
        va_end(arguments);
        valid = length > 0 && (size_t)length < sizeof(entry->name) &&
                tp_compose_printable(entry->name, sizeof(entry->name) - 1);
        if (valid)
        {
            strcpy(entry->path, path);
            strcpy(entry->sha256, sha256);
            entry->bytes = bytes;
            ++*count;
        }
    }
    return valid;
}

static int tp_compose_store_entry_add(TpComposeState* state, TpComposeEntry* entries, unsigned* count,
                                      unsigned capacity, char const* path, char const* format, unsigned index)
{
    TpRetirementStoredFile const* file = tp_compose_store_entry(state->store, path);
    int valid = file && tp_compose_entry(entries, count, capacity, file->path, file->bytes, file->sha256, format, index);
    return valid;
}

/* The validator's exact sealed closure (_sealed_closure_files): the prior
 * pre-replay identities plus every composed and streamed artifact, sorted
 * by name, with unique names and paths; the outer record binds its root. */
static int tp_compose_seal(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    unsigned capacity = request->prior_count + request->transcript_count + request->metrics_count +
                        request->untimed_metrics_count + request->sample_counts[0] + request->sample_counts[1] +
                        state->partitions + 8;
    TpComposeEntry* entries = (TpComposeEntry*)malloc((size_t)capacity * sizeof(*entries));
    unsigned* order = (unsigned*)malloc((size_t)capacity * sizeof(*order));
    unsigned count = 0;
    int valid = entries && order;
    for (unsigned i = 0; valid && i < request->prior_count; ++i)
        valid = tp_compose_entry(entries, &count, capacity, request->prior[i].path, request->prior[i].bytes,
                                 request->prior[i].sha256, "%s", request->prior[i].name);
    valid = valid &&
        tp_compose_entry(entries, &count, capacity, state->bundle.path, state->bundle.bytes, state->bundle.sha256,
                         "workflow.result_bundle") &&
        tp_compose_entry(entries, &count, capacity, state->series.path, state->series.bytes, state->series.sha256,
                         "workflow.adapter_input") &&
        tp_compose_entry(entries, &count, capacity, state->replay.path, state->replay.bytes, state->replay.sha256,
                         "workflow.adapter_result") &&
        tp_compose_entry(entries, &count, capacity, state->receipt.path, state->receipt.bytes, state->receipt.sha256,
                         "workflow.execution_receipt") &&
        tp_compose_entry(entries, &count, capacity, state->code.path, state->code.bytes, state->code.sha256,
                         "workflow.code_records");
    for (unsigned i = 0; valid && i < request->transcript_count; ++i)
        valid = tp_compose_store_entry_add(state, entries, &count, capacity, request->transcript_paths[i],
                                           "execution.shard.%u", i);
    for (unsigned i = 0; valid && i < request->metrics_count; ++i)
        valid = tp_compose_store_entry_add(state, entries, &count, capacity, request->metrics_paths[i],
                                           "execution.metrics_shard.%u", i);
    if (valid && request->layout->untimed_groups)
    {
        valid = tp_compose_entry(entries, &count, capacity, state->untimed.path, state->untimed.bytes,
                                 state->untimed.sha256, "workflow.untimed_batches");
        for (unsigned i = 0; valid && i < request->untimed_metrics_count; ++i)
            valid = tp_compose_store_entry_add(state, entries, &count, capacity, request->untimed_metrics_paths[i],
                                               "untimed.metrics_shard.%u", i);
    }
    for (unsigned g = 0; valid && g < state->partitions; ++g)
    {
        unsigned population = state->partition_population[g];
        valid = tp_compose_entry(entries, &count, capacity, state->manifests[g].path, state->manifests[g].bytes,
                                 state->manifests[g].sha256, "result_input.manifest.%s", state->partition_plan[g]->identity);
        for (unsigned s = 0; valid && s < state->partition_shards[g]; ++s)
        {
            unsigned index = state->partition_first[g] + s;
            TpComposeShard const* shard = state->samples[population] + index;
            valid = tp_compose_entry(entries, &count, capacity, shard->path, shard->bytes, shard->sha256,
                                     population ? "result_input.shard.batches-%04u" : "result_input.shard.samples-%04u",
                                     index);
        }
    }
    for (unsigned i = 0; valid && i < count; ++i) order[i] = i;
    valid = valid && tp_compose_sort(order, count, entries[0].name, sizeof(TpComposeEntry));
    for (unsigned i = 1; valid && i < count; ++i)
        valid = strcmp(entries[order[i - 1]].name, entries[order[i]].name) < 0;
    for (unsigned i = 0; valid && i < count; ++i)
        for (unsigned j = i + 1; valid && j < count; ++j) valid = strcmp(entries[i].path, entries[j].path) != 0;
    /* The files array is hashed alone for the root and embedded verbatim. */
    size_t array_capacity = (size_t)count * TP_COMPOSE_SEAL_ENTRY_BYTES + 16, array_length = 0;
    char* array = valid ? (char*)malloc(array_capacity) : NULL;
    valid = valid && array;
    for (unsigned i = 0; valid && i <= count; ++i)
    {
        int length;
        if (i == count) length = snprintf(array + array_length, array_capacity - array_length, "]");
        else
        {
            TpComposeEntry const* entry = entries + order[i];
            length = snprintf(array + array_length, array_capacity - array_length,
                "%s{\"bytes\":%" PRIu64 ",\"name\":\"%s\",\"path\":\"%s\",\"sha256\":\"%s\"}", i ? "," : "[",
                entry->bytes, entry->name, entry->path, entry->sha256);
        }
        valid = length > 0 && (size_t)length < array_capacity - array_length;
        if (valid) array_length += (size_t)length;
    }
    char root[65];
    if (valid) tp_compose_sha_hex(array, array_length, root);
    TpComposeWriter writer;
    if (valid)
    {
        tp_compose_writer_begin(&writer, state->store, request->sealed_path, state->bounds.seal, NULL);
        tp_compose_writer_format(&writer, "{\"family_sha256\":\"%s\",\"post_aa_binding_sha256\":\"%s\",\"result_bundle\":",
                                 request->family_sha256, request->post_aa_binding_sha256);
        tp_compose_descriptor(&writer, &state->bundle);
        tp_compose_writer_format(&writer, ",\"result_input_plan_sha256\":\"%s\","
            "\"schema\":\"buster-native-retirement-sealed-result-v1\",\"seal\":{\"files\":", request->result_input_plan_sha256);
        tp_compose_writer_bytes(&writer, array, array_length);
        tp_compose_writer_format(&writer, ",\"root_sha256\":\"%s\",\"schema\":\"buster-native-retirement-result-seal-v1\","
            "\"version\":1},\"status\":\"sealed-for-independent-replay\",\"version\":1}\n", root);
        valid = tp_compose_writer_publish(&writer, &state->sealed);
    }
    state->seal_entries = valid ? count : 0;
    free(array);
    free(order);
    free(entries);
    return valid;
}

/* ------------------------------------------------------------ entry point */

static int tp_compose_request_check(TpRetirementComposeRequest const* request)
{
    TpRetirementPlan const* plan = request ? request->statistics : NULL;
    int valid = request && request->store && request->layout && plan && request->sealed_path &&
                tp_retirement_token(request->job) && tp_retirement_token(request->boot) && request->attempt &&
                request->bound_at_ns && request->completed_at_ns > request->bound_at_ns &&
                tp_compose_digest(request->execution_plan_sha256) && tp_compose_digest(request->source_rows_sha256) &&
                tp_compose_digest(request->result_input_plan_sha256) && tp_compose_digest(request->family_sha256) &&
                tp_compose_digest(request->post_aa_binding_sha256) && tp_retirement_receipt_path(request->sealed_path) &&
                plan->version == TP_RETIREMENT_STATISTICS_VERSION && plan->frozen_before_samples == 1 && plan->seed &&
                plan->pairs_per_round >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND &&
                plan->pairs_per_round <= TP_RETIREMENT_EXECUTION_MAX_PAIRS && !(plan->pairs_per_round & 1) &&
                plan->resamples >= TP_RETIREMENT_MIN_RESAMPLES && plan->resamples <= TP_RETIREMENT_MAX_RESAMPLES &&
                request->metrics_count <= TP_RETIREMENT_METRICS_SHARDS &&
                request->untimed_metrics_count <= TP_RETIREMENT_METRICS_SHARDS;
    return valid;
}

int tp_retirement_compose(TpRetirementComposeRequest const* request, TpRetirementComposeResult* result)
{
    TpComposeState state = {0};
    char const* stage = "request";
    state.request = request;
    int valid = result && tp_compose_request_check(request) && !request->store->failed && request->store->planned &&
                !request->store->active;
    if (result) *result = (TpRetirementComposeResult){0};
    state.store = valid ? request->store : NULL;
    state.pairs = valid ? request->statistics->pairs_per_round : 0;
    if (valid)
    {
        stage = "family";
        valid = tp_compose_family_build(request->layout, &state.family);
    }
    TpRetirementComposeShape shape = {request ? request->layout : NULL, state.pairs,
                                      request ? request->code_count : 0, request ? request->prior_count : 0};
    if (valid)
    {
        stage = "bounds";
        valid = tp_compose_bounds_of(&shape, &state.family, &state.bounds) &&
                request->statistics->bootstrap_members_per_scope == state.family.bootstrap &&
                request->statistics->cell_members_per_scope == state.family.cells_total;
    }
    uint64_t per_unit = (uint64_t)TP_RETIREMENT_ROUNDS * state.pairs;
    for (unsigned m = 0; valid && m < TP_RETIREMENT_COMPOSE_METRICS; ++m)
    {
        state.ratios[m] = (double*)malloc((size_t)(state.family.cells[m] * per_unit) * sizeof(double));
        valid = state.ratios[m] != NULL;
    }
    size_t shard_count = valid ? (size_t)request->transcript_count + request->sample_counts[0] + request->sample_counts[1] : 0;
    state.shards = valid && shard_count ? (TpComposeShard*)calloc(shard_count, sizeof(*state.shards)) : NULL;
    valid = valid && state.shards;
    if (valid)
    {
        state.transcript = state.shards;
        state.samples[0] = state.transcript + request->transcript_count;
        state.samples[1] = state.samples[0] + request->sample_counts[0];
        stage = "inventory";
        valid = tp_compose_inventory(&state);
    }
    char timed_tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1] = {0}, untimed_tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1] = {0};
    if (valid)
    {
        stage = "metrics-shards";
        valid = tp_compose_tiling_init(&state.timed, state.store, request->metrics_paths, request->metrics_count,
                                       timed_tag) &&
                tp_compose_tiling_init(&state.untimed_tiling, state.store, request->untimed_metrics_paths,
                                       request->untimed_metrics_count, untimed_tag) &&
                (!request->metrics_count || !request->untimed_metrics_count || strcmp(timed_tag, untimed_tag));
    }
    if (valid)
    {
        stage = "prior";
        valid = tp_compose_prior(&state);
    }
    if (valid)
    {
        stage = "transcript";
        valid = tp_compose_transcript(&state);
    }
    if (valid)
    {
        stage = "untimed";
        valid = tp_compose_untimed(&state);
    }
    if (valid)
    {
        stage = "samples";
        valid = tp_compose_samples(&state);
    }
    if (valid)
    {
        stage = "partitions";
        valid = tp_compose_partitions(&state);
    }
    if (valid)
    {
        stage = "context";
        valid = tp_compose_context(&state);
    }
    /* Every input is verified: settle the reservation to the exact final
     * inventory, then publish the composer's outputs. */
    if (valid)
    {
        stage = "settle";
        valid = request->later_files <= TP_RETIREMENT_STORE_FILES &&
                tp_retirement_store_settle(state.store, state.store->count + state.bounds.files + request->later_files);
    }
    if (valid)
    {
        stage = "manifests";
        valid = tp_compose_manifests(&state);
    }
    if (valid)
    {
        stage = "code";
        valid = tp_compose_code(&state);
    }
    if (valid)
    {
        stage = "series";
        valid = tp_compose_series(&state);
    }
    if (valid)
    {
        stage = "adapter";
        valid = tp_compose_adapter(&state);
    }
    if (valid)
    {
        stage = "receipt";
        valid = tp_compose_receipt(&state);
    }
    if (valid)
    {
        stage = "bundle";
        valid = tp_compose_bundle(&state);
    }
    if (valid)
    {
        stage = "seal";
        valid = tp_compose_seal(&state);
    }
    if (valid)
    {
        stage = "validate";
        valid = request->later_files || tp_retirement_store_validate(state.store);
    }
    if (valid)
    {
        strcpy(result->raw_measurements_sha256, state.raw_sha256);
        strcpy(result->context_sha256, state.context_sha256);
        result->receipt = state.receipt;
        result->bundle = state.bundle;
        result->sealed = state.sealed;
        result->series = state.series;
        result->replay = state.replay;
        result->code = state.code;
        result->members = state.family.count;
        result->seal_entries = state.seal_entries;
        result->invocations = state.invocations;
    }
    else
    {
        if (result) result->refused = stage;
        tp_compose_tiling_abandon(&state.timed);
        tp_compose_tiling_abandon(&state.untimed_tiling);
        if (request && request->store) request->store->failed = 1;
    }
    for (unsigned m = 0; m < TP_RETIREMENT_COMPOSE_METRICS; ++m) free(state.ratios[m]);
    free(state.shards);
    tp_compose_family_free(&state.family);
    return valid;
}
#endif
