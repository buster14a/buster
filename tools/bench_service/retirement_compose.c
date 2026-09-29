/* #881-E production result composer (A1): lane D's published streams -> the
 * #511 binding's sealed result. See retirement_compose.h for the contract.
 *
 * Map (searchable symbols):
 *   helpers        tp_compose_cursor_*, tp_compose_field, tp_compose_json_number,
 *                  tp_compose_decimal_ratio, tp_compose_sort, tp_compose_sort_text
 *   family         tp_compose_layout_check, tp_compose_family_build,
 *                  tp_compose_bounds_of
 *   declaration    tp_compose_declaration_digest, tp_compose_declaration_reserve
 *   readers        TpComposeReader, tp_compose_reader_*, TpComposeTiling,
 *                  tp_compose_tiling_*, tp_compose_metrics_input
 *   inputs         tp_compose_inventory, tp_compose_prior, tp_compose_transcript
 *                  (tp_compose_invocation, tp_compose_observe), tp_compose_untimed,
 *                  tp_compose_aa (A/A metrics-shard tiling),
 *                  tp_compose_samples, tp_compose_partitions, tp_compose_context
 *   outputs        TpComposeWriter, tp_compose_manifests, tp_compose_code,
 *                  TpComposeSeries (tp_compose_series_plan before the settle,
 *                  tp_compose_series_pass, tp_compose_series: the #1880 series
 *                  shards and manifest), tp_compose_adapter (tp_compose_adapter_run,
 *                  tp_compose_replay_check), tp_compose_receipt,
 *                  tp_compose_retained, tp_compose_bundle, tp_compose_seal
 *   entry points   tp_retirement_compose_bounds, tp_retirement_compose_plan,
 *                  tp_retirement_compose
 *
 * The line formats checked here are lane D's canonical encoders
 * (retirement_execution.h, retirement_samples.h, retirement_untimed.h). The
 * composer re-reads every input through its sealed store inode, rehashes it,
 * cross-checks the frozen schedule with D's own #619 cursor, and joins every
 * numeric sample to the transcript observation (and, for object members,
 * the metrics input) that produced it; the binding validator remains the
 * independent authority over every semantic join. All working memory comes
 * from one arena per call whose reservation is checked before each
 * allocation.
 */
#define _GNU_SOURCE 1
#include "retirement_compose.h"
#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* "ratio=" + the longest %.17g double (23 bytes) + LF, rounded up. */
#define TP_COMPOSE_RATIO_LINE_BYTES 32u
/* One adapter member header line: a 127-byte identity and six numbers. */
#define TP_COMPOSE_MEMBER_LINE_BYTES 256u
/* The series header line and the member terminator ("end\n"). */
#define TP_COMPOSE_SERIES_HEADER_BYTES 256u
#define TP_COMPOSE_SERIES_END_BYTES 4u
/* (#1880) No series line is longer than this (the header and member lines
 * are the longest), so under greedy packing every shard but the last holds
 * more than shard_bytes - TP_COMPOSE_SERIES_LINE_BYTES bytes. */
#define TP_COMPOSE_SERIES_LINE_BYTES 256u
/* The series manifest: header and series line, then one line per shard. */
#define TP_COMPOSE_SERIES_MANIFEST_FIXED_BYTES 512u
#define TP_COMPOSE_SERIES_MANIFEST_LINE_BYTES 256u
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
/* One retained-manifest line: kind, digest, byte count and path. */
#define TP_COMPOSE_RETAINED_LINE_BYTES 320u
#define TP_COMPOSE_NUMBER_BYTES 48u
#define TP_COMPOSE_LINE_BYTES 8192u
/* Decimal('a') / Decimal('b') under Python's default 28-digit context. */
#define TP_COMPOSE_DECIMAL_DIGITS 28u
/* Code-byte ratios are computed in doubles; exact below 2^53. */
#define TP_COMPOSE_CODE_BYTES_MAX (UINT64_C(1) << 53)
/* Code-byte totals stay below 2^60, so decimal long division never wraps. */
#define TP_COMPOSE_TOTAL_BYTES_MAX (UINT64_C(1) << 60)
/* D's peak-memory and RSS samples are exact integers in a double. */
#define TP_COMPOSE_MEMORY_MAX UINT64_C(9007199254740991)
/* The per-call working reservation (virtual; committed as used). */
#define TP_COMPOSE_ARENA_BYTES (UINT64_C(1) << 30)
/* The post-A/A binding document read for the execution context. */
#define TP_COMPOSE_BINDING_BYTES (UINT64_C(4) << 20)
/* The reviewed adapter executable is hashed whole before it is executed. */
#define TP_COMPOSE_ADAPTER_BYTES (UINT64_C(256) << 20)
/* The adapter is polled at this interval against its wall-clock limit. */
#define TP_COMPOSE_ADAPTER_POLL_NS 10000000L
#define TP_COMPOSE_NANOSECONDS 1000000000L
/* A retained group member: `<prefix>` + four digits + `<suffix>`. */
#define TP_COMPOSE_GROUP_DIGITS 4u
#define TP_COMPOSE_SUFFIX_BYTES 32u
#define TP_COMPOSE_EMPTY_SHA256 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define TP_COMPOSE_NONE 0xffffffffu
#define TP_COMPOSE_READ_BYTES 65536u
/* The adapter's approved schema and its three #619 scopes. */
#define TP_COMPOSE_REPLAY_SCHEMA "buster-native-retirement-statistics-replay-v1"
#define TP_COMPOSE_SCOPES 3u
/* Classes of a store file during the inventory. */
#define TP_COMPOSE_CLASS_FREE 0u
#define TP_COMPOSE_CLASS_SEALED 1u
#define TP_COMPOSE_CLASS_RETAINED 2u
/* Numeric observation tables: row wall/memory, runtime, batch wall/RSS. */
#define TP_COMPOSE_ROW_WALL 0u
#define TP_COMPOSE_ROW_MEMORY 1u
#define TP_COMPOSE_RUNTIME 2u
#define TP_COMPOSE_BATCH_WALL 3u
#define TP_COMPOSE_BATCH_RSS 4u
#define TP_COMPOSE_OBSERVATIONS 5u

#ifdef BUSTER_RETIREMENT_STORE_TEST
/* Fixture-only override of the series shard size, so a small family spans
 * several shards (the validator and adapter tests patch theirs to match). */
uint64_t tp_retirement_compose_test_shard_bytes;
#endif

BUSTER_GLOBAL_LOCAL uint64_t tp_compose_series_shard_bytes(void)
{
    uint64_t result = TP_RETIREMENT_COMPOSE_SERIES_SHARD_BYTES;
#ifdef BUSTER_RETIREMENT_STORE_TEST
    if (tp_retirement_compose_test_shard_bytes) result = tp_retirement_compose_test_shard_bytes;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL char const* const tp_compose_metric_names[TP_RETIREMENT_COMPOSE_METRICS] = {
    "compiler_wall_time", "compiler_peak_memory", "generated_runtime",
    "compiler_batch_wall_time", "compiler_batch_peak_rss"};
BUSTER_GLOBAL_LOCAL char const* const tp_compose_aggregate_limits[TP_RETIREMENT_COMPOSE_METRICS] = {
    "1.02", "1.02", "1.03", "1.02", "1.02"};
BUSTER_GLOBAL_LOCAL char const* const tp_compose_cell_limits[TP_RETIREMENT_COMPOSE_METRICS] = {
    "1.05", "1.05", "1.03", "1.05", "1.05"};
BUSTER_GLOBAL_LOCAL char const* const tp_compose_dimension_names[TP_RETIREMENT_COMPOSE_DIMENSIONS] = {
    "target", "cpu", "allocator", "frontend_lowering", "PIC", "artifact_stage"};

/* ---------------------------------------------------------------- helpers */

BUSTER_GLOBAL_LOCAL int tp_compose_digest(char const* text)
{
    int valid = text && strnlen(text, 65) == 64;
    for (unsigned i = 0; valid && i < 64; ++i)
        valid = (text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f');
    return valid;
}

/* A JSON-safe printable identifier: no space, quote, backslash or control. */
BUSTER_GLOBAL_LOCAL int tp_compose_printable(char const* text, size_t capacity)
{
    size_t length = text ? strnlen(text, capacity + 1) : 0;
    int valid = length && length <= capacity;
    for (size_t i = 0; valid && i < length; ++i)
        valid = text[i] > 0x20 && text[i] < 0x7f && text[i] != '"' && text[i] != '\\';
    return valid;
}

/* A normalized relative evidence path (the validator's _relative_path). */
BUSTER_GLOBAL_LOCAL int tp_compose_relative_path(char const* path)
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
BUSTER_GLOBAL_LOCAL int tp_compose_member_text(char const* text, int first)
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

BUSTER_GLOBAL_LOCAL void tp_compose_sha_hex(void const* bytes, size_t length, char output[65])
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

BUSTER_GLOBAL_LOCAL void tp_compose_cursor_literal(TpComposeCursor* cursor, char const* literal)
{
    size_t length = strlen(literal);
    cursor->valid = cursor->valid && cursor->offset <= cursor->length && length <= cursor->length - cursor->offset &&
                    !memcmp(cursor->bytes + cursor->offset, literal, length);
    if (cursor->valid) cursor->offset += length;
}

BUSTER_GLOBAL_LOCAL void tp_compose_cursor_u64(TpComposeCursor* cursor, uint64_t* output)
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
BUSTER_GLOBAL_LOCAL void tp_compose_cursor_text(TpComposeCursor* cursor, char* output, size_t capacity)
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
BUSTER_GLOBAL_LOCAL int tp_compose_json_number(char const* text, size_t length, double* value)
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

/* A number up to the next `,` or `}`: its text span and its value. */
BUSTER_GLOBAL_LOCAL void tp_compose_cursor_number(TpComposeCursor* cursor, char const** text, size_t* length,
                                                  double* value)
{
    size_t start = cursor->offset;
    while (cursor->valid && cursor->offset < cursor->length && cursor->bytes[cursor->offset] != ',' &&
           cursor->bytes[cursor->offset] != '}')
        ++cursor->offset;
    cursor->valid = cursor->valid && tp_compose_json_number(cursor->bytes + start, cursor->offset - start, value);
    *text = cursor->bytes + start;
    *length = cursor->valid ? cursor->offset - start : 0;
    if (!cursor->valid) *value = 0.0;
}

/* Locate a top-level `"key":` in one canonical JSON object line and return
 * its value span. Keys are unique and values carry no escapes. */
BUSTER_GLOBAL_LOCAL int tp_compose_field(char const* line, size_t length, char const* key, char const** value,
                                         size_t* value_length)
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

BUSTER_GLOBAL_LOCAL int tp_compose_field_u64(char const* line, size_t length, char const* key, uint64_t* output)
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

BUSTER_GLOBAL_LOCAL int tp_compose_field_is(char const* line, size_t length, char const* key, char const* expected)
{
    char const* value = NULL;
    size_t count = 0;
    int valid = tp_compose_field(line, length, key, &value, &count) && count == strlen(expected) &&
                !memcmp(value, expected, count);
    return valid;
}

/* A quoted string field copied without its quotes. */
BUSTER_GLOBAL_LOCAL int tp_compose_field_text(char const* line, size_t length, char const* key, char* output,
                                              size_t capacity)
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
BUSTER_GLOBAL_LOCAL int tp_compose_field_optional(char const* line, size_t length, char const* key, int none,
                                                  uint64_t expected)
{
    uint64_t value = 0;
    int valid = none ? tp_compose_field_is(line, length, key, "null") :
                tp_compose_field_u64(line, length, key, &value) && value == expected;
    return valid;
}

/* float(Decimal(numerator) / Decimal(denominator)) under the default
 * 28-digit ROUND_HALF_EVEN context, then correctly rounded to a double. */
BUSTER_GLOBAL_LOCAL int tp_compose_decimal_ratio(uint64_t numerator, uint64_t denominator, double* value)
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

/* Iterative bottom-up merge sort of `order` by the name at a fixed stride
 * (strcmp: Python's code-point order for these ASCII names). */
BUSTER_GLOBAL_LOCAL void tp_compose_sort(unsigned* order, unsigned* scratch, unsigned count, char const* names,
                                         size_t stride)
{
    for (unsigned width = 1; width < count; width *= 2)
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
}

/* The same sort over an array of strings. */
BUSTER_GLOBAL_LOCAL void tp_compose_sort_text(char const** items, char const** scratch, unsigned count)
{
    for (unsigned width = 1; width < count; width *= 2)
    {
        for (unsigned left = 0; left < count; left += 2 * width)
        {
            unsigned middle = left + width < count ? left + width : count;
            unsigned right = left + 2 * width < count ? left + 2 * width : count;
            unsigned a = left, b = middle, out = left;
            while (a < middle || b < right)
            {
                int take_left = b >= right || (a < middle && strcmp(items[a], items[b]) <= 0);
                scratch[out++] = take_left ? items[a++] : items[b++];
            }
        }
        memcpy(items, scratch, (size_t)count * sizeof(*items));
    }
}

BUSTER_GLOBAL_LOCAL int tp_compose_add(uint64_t* total, uint64_t value)
{
    int valid = value <= UINT64_MAX - *total;
    if (valid) *total += value;
    return valid;
}

BUSTER_GLOBAL_LOCAL Arena* tp_compose_arena(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = TP_COMPOSE_ARENA_BYTES, .flags = {.no_pool = 1}});
    return arena;
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
    unsigned* group_offset;
    unsigned* group_rows;
    unsigned* group_object;
    unsigned* object_groups;
    unsigned* runtime_dense;
    unsigned* runtime_ids;
    unsigned* runtime_index;
    unsigned count, capacity, bootstrap, cells_total, object_count, runtime_count;
    unsigned cells[TP_RETIREMENT_COMPOSE_METRICS];
} TpComposeFamily;

/* The frozen A1 shape: ascending rows, groups numbered by smallest member,
 * singletons of one non-object row, object rows sharing configuration. */
BUSTER_GLOBAL_LOCAL int tp_compose_layout_check(TpRetirementComposeLayout const* layout, Arena* arena)
{
    int valid = layout && layout->rows && layout->group_kinds && layout->row_count && layout->group_count &&
                layout->row_count <= TP_RETIREMENT_MAX_CELLS && layout->group_count <= layout->row_count &&
                layout->population_rows <= TP_RETIREMENT_MAX_CELLS && layout->untimed_groups <= TP_RETIREMENT_MAX_CELLS;
    unsigned* first = valid ? (unsigned*)tp_retirement_compose_allocate(arena,
                                  (uint64_t)layout->group_count * 2 * sizeof(unsigned)) : NULL;
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
        valid = (layout->group_kinds[g] == TP_RETIREMENT_GROUP_OBJECT && members[g] <= TP_RETIREMENT_BATCH_INPUTS) ||
                members[g] == 1;
    return valid;
}

/* Dimension `d` of cell `cell` of metric `metric`: a timed row's identity or,
 * for the batch pair, its object group's (the group's first member). */
BUSTER_GLOBAL_LOCAL char const* tp_compose_cell_value(TpComposeFamily const* family, unsigned metric, unsigned cell,
                                                      unsigned d)
{
    TpRetirementComposeLayout const* layout = family->layout;
    unsigned dense = metric < 2 ? cell : metric == 2 ? family->runtime_dense[cell] :
                     family->group_first[family->object_groups[cell]];
    return layout->rows[dense].dimensions[d];
}

BUSTER_GLOBAL_LOCAL int tp_compose_member_add(TpComposeFamily* family, unsigned metric, unsigned kind,
                                              unsigned dimension, unsigned unit, unsigned cells, char const* value,
                                              char const* format, ...)
{
    int valid = family->count < family->capacity;
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
            member->cells = cells;
            ++family->count;
        }
    }
    return valid;
}

/* The validator's _derive_statistical_family over the timed projection:
 * per metric an aggregate, one slice per present dimension value (distinct
 * values found by sorting, each with its cell count), one cell per eligible
 * row (wall/memory: timed rows; runtime: runtime rows) or object group (the
 * batch pair); members sorted; bootstrap and cell family indexes are the
 * ordinals among aggregate/slice and cell members. The group tables (member
 * rows per group, object ordinal per group) serve the transcript join. */
BUSTER_GLOBAL_LOCAL int tp_compose_family_build(TpRetirementComposeLayout const* layout, TpComposeFamily* family,
                                                Arena* arena)
{
    *family = (TpComposeFamily){.layout = layout};
    int valid = tp_compose_layout_check(layout, arena);
    unsigned rows = valid ? layout->row_count : 0, groups = valid ? layout->group_count : 0;
    uint64_t slots = valid ? (uint64_t)groups * 4 + 1 + (uint64_t)rows * 4 : 0;
    family->group_first = valid ? (unsigned*)tp_retirement_compose_allocate(arena, slots * sizeof(unsigned)) : NULL;
    valid = valid && family->group_first;
    if (valid)
    {
        family->group_offset = family->group_first + groups;
        family->group_object = family->group_offset + groups + 1;
        family->object_groups = family->group_object + groups;
        family->group_rows = family->object_groups + groups;
        family->runtime_dense = family->group_rows + rows;
        family->runtime_ids = family->runtime_dense + rows;
        family->runtime_index = family->runtime_ids + rows;
        for (unsigned g = 0; g < groups; ++g)
        {
            family->group_first[g] = TP_COMPOSE_NONE;
            family->group_object[g] = TP_COMPOSE_NONE;
        }
        for (unsigned r = 0; r < rows; ++r)
        {
            TpRetirementComposeRow const* row = layout->rows + r;
            family->runtime_index[r] = TP_COMPOSE_NONE;
            if (family->group_first[row->group] == TP_COMPOSE_NONE) family->group_first[row->group] = r;
            ++family->group_offset[row->group + 1];
            if (row->runtime)
            {
                family->runtime_index[r] = family->runtime_count;
                family->runtime_ids[family->runtime_count] = row->id;
                family->runtime_dense[family->runtime_count++] = r;
            }
        }
        for (unsigned g = 0; g < groups; ++g) family->group_offset[g + 1] += family->group_offset[g];
        /* Members in ascending census order; object_groups is scratch. */
        for (unsigned g = 0; g < groups; ++g) family->object_groups[g] = family->group_offset[g];
        for (unsigned r = 0; r < rows; ++r) family->group_rows[family->object_groups[layout->rows[r].group]++] = r;
        for (unsigned g = 0; g < groups; ++g)
            if (layout->group_kinds[g] == TP_RETIREMENT_GROUP_OBJECT)
            {
                family->group_object[g] = family->object_count;
                family->object_groups[family->object_count++] = g;
            }
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
    /* Aggregates and slices share the bootstrap cap; cells are exact. */
    family->capacity = valid ? TP_RETIREMENT_COMPOSE_BOOTSTRAP_MEMBERS + family->cells_total : 0;
    family->members = valid ? (TpComposeMember*)tp_retirement_compose_allocate(arena,
                                  (uint64_t)family->capacity * sizeof(TpComposeMember)) : NULL;
    family->order = valid ? (unsigned*)tp_retirement_compose_allocate(arena,
                                (uint64_t)family->capacity * 2 * sizeof(unsigned)) : NULL;
    char const** values = valid ? (char const**)tp_retirement_compose_allocate(arena,
                              (uint64_t)rows * 2 * sizeof(char const*)) : NULL;
    valid = valid && family->members && family->order && values;
    for (unsigned m = 0; valid && m < TP_RETIREMENT_COMPOSE_METRICS; ++m)
    {
        char const* metric = tp_compose_metric_names[m];
        unsigned cells = family->cells[m];
        valid = tp_compose_member_add(family, m, 0, TP_COMPOSE_NONE, TP_COMPOSE_NONE, cells, NULL, "%s/aggregate", metric);
        for (unsigned d = 0; valid && d < TP_RETIREMENT_COMPOSE_DIMENSIONS; ++d)
        {
            for (unsigned cell = 0; cell < cells; ++cell) values[cell] = tp_compose_cell_value(family, m, cell, d);
            tp_compose_sort_text(values, values + rows, cells);
            unsigned run = 0;
            for (unsigned cell = 0; valid && cell < cells; ++cell)
            {
                ++run;
                if (cell + 1 == cells || strcmp(values[cell], values[cell + 1]))
                {
                    valid = tp_compose_member_add(family, m, 0, d, TP_COMPOSE_NONE, run, values[cell], "%s/slice/%s=%s",
                                                  metric, tp_compose_dimension_names[d], values[cell]);
                    run = 0;
                }
            }
        }
        for (unsigned cell = 0; valid && cell < cells; ++cell)
            valid = m < 2 ? tp_compose_member_add(family, m, 1, TP_COMPOSE_NONE, cell, 1, NULL, "%s/cell/row=%u", metric,
                                                  layout->rows[cell].id) :
                    m == 2 ? tp_compose_member_add(family, m, 1, TP_COMPOSE_NONE, cell, 1, NULL, "%s/cell/row=%u",
                                                   metric, family->runtime_ids[cell]) :
                             tp_compose_member_add(family, m, 1, TP_COMPOSE_NONE, cell, 1, NULL, "%s/cell/group=%u",
                                                   metric, family->object_groups[cell]);
    }
    for (unsigned i = 0; valid && i < family->count; ++i) family->order[i] = i;
    if (valid)
        tp_compose_sort(family->order, family->order + family->capacity, family->count, family->members[0].name,
                        sizeof(TpComposeMember));
    unsigned cells = 0;
    for (unsigned i = 0; valid && i < family->count; ++i)
    {
        TpComposeMember* member = family->members + family->order[i];
        valid = !i || strcmp(family->members[family->order[i - 1]].name, member->name) < 0;
        member->family = member->kind ? cells++ : family->bootstrap++;
    }
    valid = valid && family->bootstrap && family->bootstrap <= TP_RETIREMENT_COMPOSE_BOOTSTRAP_MEMBERS &&
            cells == family->cells_total;
    if (!valid) *family = (TpComposeFamily){0};
    return valid;
}

/* Every composer output's byte bound, each at most one store file. */
BUSTER_GLOBAL_LOCAL int tp_compose_bounds_of(TpRetirementComposeShape const* shape, TpComposeFamily const* family,
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
        /* (#1880) Greedy whole-line packing: n shards hold more than
         * (n - 1) * (shard_bytes - line_max) bytes. */
        uint64_t shard_bytes = tp_compose_series_shard_bytes();
        valid = valid && shard_bytes > TP_COMPOSE_SERIES_LINE_BYTES && shard_bytes <= TP_RETIREMENT_STORE_FILE_BYTES;
        uint64_t shards = valid ? 1 + (result.series - 1) / (shard_bytes - TP_COMPOSE_SERIES_LINE_BYTES + 1) : 0;
        valid = valid && shards <= TP_RETIREMENT_COMPOSE_SERIES_SHARDS;
        result.series_shards = valid ? (unsigned)shards : 0;
        result.series_manifest = TP_COMPOSE_SERIES_MANIFEST_FIXED_BYTES + shards * TP_COMPOSE_SERIES_MANIFEST_LINE_BYTES;
        result.replay = TP_COMPOSE_REPLAY_FIXED_BYTES + (uint64_t)family->count * TP_COMPOSE_REPLAY_MEMBER_BYTES;
        result.bundle = TP_COMPOSE_BUNDLE_BYTES;
        result.receipt = TP_RETIREMENT_RECEIPT_BYTES;
        result.retained = sizeof(TP_RETIREMENT_RETAINED_MANIFEST_HEADER) +
                          (uint64_t)TP_RETIREMENT_STORE_FILES * TP_COMPOSE_RETAINED_LINE_BYTES;
        result.seal = TP_COMPOSE_SEAL_FIXED_BYTES +
            (uint64_t)(shape->prior_entries + TP_RETIREMENT_STORE_FILES) * TP_COMPOSE_SEAL_ENTRY_BYTES;
        result.files = result.manifest_count + TP_RETIREMENT_COMPOSE_FIXED_OUTPUTS + result.series_shards;
    }
    /* Every output is at most one store file; the adapter input is sharded
     * (#1880), so only its manifest is one file here. */
    valid = valid && result.manifests <= TP_RETIREMENT_STORE_FILE_BYTES && result.code <= TP_RETIREMENT_STORE_FILE_BYTES &&
            result.series_manifest <= TP_RETIREMENT_STORE_FILE_BYTES && result.replay <= TP_RETIREMENT_STORE_FILE_BYTES &&
            result.retained <= TP_RETIREMENT_STORE_FILE_BYTES && result.seal <= TP_RETIREMENT_STORE_FILE_BYTES;
    valid = valid && tp_compose_add(&result.total, result.manifests) && tp_compose_add(&result.total, result.code) &&
            tp_compose_add(&result.total, result.series) && tp_compose_add(&result.total, result.series_manifest) &&
            tp_compose_add(&result.total, result.replay) &&
            tp_compose_add(&result.total, result.bundle) && tp_compose_add(&result.total, result.receipt) &&
            tp_compose_add(&result.total, result.retained) && tp_compose_add(&result.total, result.seal);
    *bounds = valid ? result : (TpRetirementComposeBounds){0};
    return valid;
}

int tp_retirement_compose_bounds(TpRetirementComposeShape const* shape, TpRetirementComposeBounds* bounds)
{
    TpComposeFamily family;
    Arena* arena = shape && bounds ? tp_compose_arena() : NULL;
    int valid = arena && tp_compose_family_build(shape->layout, &family, arena) &&
                tp_compose_bounds_of(shape, &family, bounds);
    if (arena) arena_destroy(arena, 1);
    if (bounds && !valid) *bounds = (TpRetirementComposeBounds){0};
    return valid;
}

/* ------------------------------------------------------------ declaration */

BUSTER_GLOBAL_LOCAL int tp_compose_kind(char const* kind)
{
    size_t length = kind ? strnlen(kind, TP_RETIREMENT_COMPOSE_KIND_BYTES + 1) : 0;
    int valid = length && length <= TP_RETIREMENT_COMPOSE_KIND_BYTES;
    for (size_t i = 0; valid && i < length; ++i) valid = kind[i] >= 'a' && kind[i] <= 'z';
    return valid;
}

/* Paths the composer publishes itself (and the producer's receipt),
 * including every series shard name. */
BUSTER_GLOBAL_LOCAL int tp_compose_reserved_path(char const* path)
{
    size_t prefix = sizeof(TP_RETIREMENT_COMPOSE_SERIES_SHARD_PREFIX) - 1;
    size_t suffix = sizeof(TP_RETIREMENT_COMPOSE_SERIES_SHARD_SUFFIX) - 1;
    int shard = strlen(path) == prefix + TP_COMPOSE_GROUP_DIGITS + suffix &&
                !memcmp(path, TP_RETIREMENT_COMPOSE_SERIES_SHARD_PREFIX, prefix) &&
                !memcmp(path + prefix + TP_COMPOSE_GROUP_DIGITS, TP_RETIREMENT_COMPOSE_SERIES_SHARD_SUFFIX, suffix);
    for (unsigned i = 0; shard && i < TP_COMPOSE_GROUP_DIGITS; ++i)
        shard = path[prefix + i] >= '0' && path[prefix + i] <= '9';
    int reserved = shard || !strcmp(path, TP_RETIREMENT_EXECUTION_RECEIPT_PATH) ||
                   !strcmp(path, TP_RETIREMENT_RETAINED_MANIFEST_PATH) || !strcmp(path, TP_RETIREMENT_COMPOSE_CODE_PATH) ||
                   !strcmp(path, TP_RETIREMENT_COMPOSE_SERIES_PATH) || !strcmp(path, TP_RETIREMENT_COMPOSE_REPLAY_PATH) ||
                   !strcmp(path, TP_RETIREMENT_COMPOSE_BUNDLE_PATH);
    return reserved;
}

/* Validate the declaration and digest its canonical text:
 *   BQ-RETIREMENT-RETAINED-DECLARATION-V1
 *   file <kind> <reserved> <bytes_max> <path>
 *   group <kind> <reserved> <files_max> <bytes_max> <prefix> =<suffix>
 *   prior <entries> <bytes>
 * in declaration order. */
BUSTER_GLOBAL_LOCAL int tp_compose_declaration_digest(TpRetirementComposeDeclaration const* declaration,
                                                      char output[65])
{
    int valid = declaration && declaration->retained_count <= TP_RETIREMENT_COMPOSE_RETAINED_ENTRIES &&
                (!declaration->retained_count || declaration->retained) &&
                declaration->prior_entries <= TP_RETIREMENT_STORE_FILES &&
                declaration->prior_bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES;
    Sha256 hash;
    sha256_init(&hash);
    char line[TP_RETIREMENT_STORE_PATH_BYTES + TP_COMPOSE_SUFFIX_BYTES + 128];
    int length = snprintf(line, sizeof(line), "BQ-RETIREMENT-RETAINED-DECLARATION-V1\n");
    sha256_add(&hash, line, (u64)length);
    for (unsigned i = 0; valid && i < declaration->retained_count; ++i)
    {
        TpRetirementComposeRetained const* entry = declaration->retained + i;
        int group = entry->suffix != NULL;
        size_t prefix = entry->prefix ? strnlen(entry->prefix, TP_RETIREMENT_STORE_PATH_BYTES + 1) : 0;
        size_t suffix = group ? strnlen(entry->suffix, TP_COMPOSE_SUFFIX_BYTES + 1) : 0;
        valid = tp_compose_kind(entry->kind) && entry->reserved <= 1 && (entry->reserved ? !entry->bytes_max :
                entry->bytes_max && entry->bytes_max <= TP_RETIREMENT_STORE_TOTAL_BYTES) &&
                tp_compose_relative_path(entry->prefix);
        if (valid && group)
        {
            valid = suffix <= TP_COMPOSE_SUFFIX_BYTES && (!suffix || tp_compose_printable(entry->suffix, suffix)) &&
                    !memchr(entry->suffix, '/', suffix) && prefix + TP_COMPOSE_GROUP_DIGITS + suffix <=
                    TP_RETIREMENT_STORE_PATH_BYTES && entry->files_max && entry->files_max <= TP_RETIREMENT_COMPOSE_GROUP_FILES;
            /* A group whose members would be series shard names is refused. */
            char member[TP_RETIREMENT_STORE_PATH_BYTES + 1];
            valid = valid && snprintf(member, sizeof(member), "%s0000%s", entry->prefix, entry->suffix) > 0 &&
                    !tp_compose_reserved_path(member);
            length = valid ? snprintf(line, sizeof(line), "group %s %u %u %" PRIu64 " %s =%s\n", entry->kind,
                                      entry->reserved, entry->files_max, entry->bytes_max, entry->prefix, entry->suffix) : 0;
        }
        else if (valid)
        {
            valid = entry->files_max == 1 && !tp_compose_reserved_path(entry->prefix);
            for (unsigned j = 0; valid && j < i; ++j)
                valid = declaration->retained[j].suffix || strcmp(declaration->retained[j].prefix, entry->prefix);
            length = valid ? snprintf(line, sizeof(line), "file %s %u %" PRIu64 " %s\n", entry->kind, entry->reserved,
                                      entry->bytes_max, entry->prefix) : 0;
        }
        valid = valid && length > 0 && (size_t)length < sizeof(line);
        if (valid) sha256_add(&hash, line, (u64)length);
    }
    length = valid ? snprintf(line, sizeof(line), "prior %u %" PRIu64 "\n", declaration->prior_entries,
                              declaration->prior_bytes) : 0;
    if (valid)
    {
        sha256_add(&hash, line, (u64)length);
        sha256_finish_hex(&hash, (char8*)output);
    }
    else output[0] = 0;
    return valid;
}

int tp_retirement_compose_plan(TpRetirementStore* store, TpRetirementCampaignCapacity const* capacity,
    TpRetirementComposeShape const* shape, TpRetirementComposeDeclaration const* declaration,
    unsigned external_entries, uint64_t external_bytes, TpRetirementCampaignStorePlan* plan)
{
    TpRetirementComposeBounds bounds = {0};
    uint64_t control_bytes = 0, control_files = 0, bounded = capacity ? capacity->total_metrics_shards_upper_bound : 0;
    char digest[65];
    int valid = store && capacity && plan && shape && tp_retirement_compose_bounds(shape, &bounds) &&
                tp_compose_declaration_digest(declaration, digest) &&
                shape->prior_entries == declaration->prior_entries &&
                tp_compose_add(&control_bytes, bounds.total) && tp_compose_add(&control_files, bounds.files);
    /* Series shards are an upper bound; at least one is always written. */
    if (valid) bounded += bounds.series_shards - 1;
    for (unsigned i = 0; valid && i < declaration->retained_count; ++i)
    {
        TpRetirementComposeRetained const* entry = declaration->retained + i;
        if (!entry->reserved)
        {
            valid = tp_compose_add(&control_files, entry->files_max) && tp_compose_add(&control_bytes, entry->bytes_max);
            if (valid && entry->suffix) bounded += entry->files_max;
        }
    }
    uint64_t externals = (uint64_t)external_entries + (valid ? declaration->prior_entries : 0);
    valid = valid && control_files <= TP_RETIREMENT_STORE_FILES && externals <= TP_RETIREMENT_STORE_FILES &&
            external_bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES && tp_compose_add(&external_bytes, declaration->prior_bytes);
    if (plan) *plan = (TpRetirementCampaignStorePlan){0};
    valid = valid && tp_retirement_campaign_store_preflight(capacity, control_files, control_bytes,
        externals, external_bytes, plan) &&
        plan->owned_files <= store->capacity && bounded <= plan->owned_files &&
        tp_retirement_store_plan(store, (unsigned)plan->owned_files, plan->owned_bytes, (unsigned)externals,
                                 external_bytes) &&
        tp_retirement_store_bound(store, (unsigned)bounded) && tp_retirement_store_retain(store, digest);
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

BUSTER_GLOBAL_LOCAL int tp_compose_reader_open(TpComposeReader* reader, TpRetirementStore* store, char const* path)
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
BUSTER_GLOBAL_LOCAL int tp_compose_reader_line(TpComposeReader* reader, char* line, size_t capacity, size_t* length)
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

/* Close after requiring the sealed byte count, end of file and digest. */
BUSTER_GLOBAL_LOCAL int tp_compose_reader_close(TpComposeReader* reader)
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

BUSTER_GLOBAL_LOCAL void tp_compose_reader_abandon(TpComposeReader* reader)
{
    if (reader->stream) fclose(reader->stream);
    reader->stream = NULL;
    reader->failed = 1;
}

/* One writer's metrics shards: artifacts must tile each declared shard from
 * offset zero in record order, shards in declared order (tag, 0000...), with
 * no declared shard left unreferenced or partly referenced. Each artifact is
 * read as D's metrics lines (a CC_METRICS header, then CC_METRICS_INPUT and
 * CC_METRICS_FUNCTION records), and its first `members` inputs yield their
 * (interval, arena high-water) samples. */
typedef struct TpComposeTiling
{
    TpRetirementStore* store;
    char const* const* paths;
    char* line;
    unsigned count, next;
    TpComposeReader reader;
    int open, failed;
    uint64_t artifacts;
} TpComposeTiling;

BUSTER_GLOBAL_LOCAL int tp_compose_shard_tag(char const* path, char tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1],
                                             unsigned* index)
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

BUSTER_GLOBAL_LOCAL int tp_compose_tiling_init(TpComposeTiling* tiling, TpRetirementStore* store,
                                               char const* const* paths, unsigned count, char* line,
                                               char tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1])
{
    *tiling = (TpComposeTiling){.store = store, .paths = paths, .count = count, .line = line};
    int valid = count <= TP_RETIREMENT_METRICS_SHARDS && (!count || paths) && line;
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

/* One CC_METRICS_INPUT record: its ordinal and, for a member, its sample. */
BUSTER_GLOBAL_LOCAL int tp_compose_metrics_input(char const* line, size_t length, unsigned ordinal,
                                                 uint64_t* interval, uint64_t* memory)
{
    TpRetirementMetricsValue values[TP_METRICS_I_COUNT];
    int valid = tp_retirement_metrics_line(line, length, "CC_METRICS_INPUT", tp_retirement_metrics_input_fields,
                                           TP_METRICS_I_COUNT, values) &&
                values[TP_METRICS_I_INDEX].number == ordinal &&
                values[TP_METRICS_I_END].number > values[TP_METRICS_I_START].number;
    if (valid && interval)
    {
        *interval = values[TP_METRICS_I_END].number - values[TP_METRICS_I_START].number;
        *memory = values[TP_METRICS_I_ARENA_PEAK].number;
        valid = *memory > 0 && *memory <= TP_COMPOSE_MEMORY_MAX;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_compose_tiling_add(TpComposeTiling* tiling, char const* path, uint64_t offset,
                                              uint64_t bytes, char const* sha256, unsigned members,
                                              uint64_t* intervals, uint64_t* memory)
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
    Sha256 hash;
    sha256_init(&hash);
    uint64_t consumed = 0;
    unsigned inputs = 0, lines = 0;
    while (valid && consumed < bytes)
    {
        size_t length = 0;
        valid = tp_compose_reader_line(&tiling->reader, tiling->line, TP_RETIREMENT_METRICS_LINE_BYTES + 1, &length) == 1 &&
                length <= bytes - consumed;
        if (valid)
        {
            sha256_add(&hash, tiling->line, (u64)length);
            consumed += length;
            size_t body = length - 1;
            if (!lines) valid = body > 11 && !memcmp(tiling->line, "CC_METRICS ", 11);
            else if (body > 17 && !memcmp(tiling->line, "CC_METRICS_INPUT ", 17))
            {
                valid = inputs < TP_RETIREMENT_BATCH_INPUTS &&
                        tp_compose_metrics_input(tiling->line, body, inputs, inputs < members ? intervals + inputs : NULL,
                                                 inputs < members ? memory + inputs : NULL);
                ++inputs;
            }
            else valid = body > 20 && !memcmp(tiling->line, "CC_METRICS_FUNCTION ", 20) && inputs;
            ++lines;
        }
    }
    if (valid)
    {
        char digest[65];
        sha256_finish_hex(&hash, (char8*)digest);
        valid = consumed == bytes && inputs >= members && !strcmp(digest, sha256);
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

BUSTER_GLOBAL_LOCAL int tp_compose_tiling_finish(TpComposeTiling* tiling)
{
    int valid = !tiling->failed;
    if (tiling->open) valid = tp_compose_reader_close(&tiling->reader) && valid;
    tiling->open = 0;
    valid = valid && tiling->next == tiling->count;
    tiling->failed = !valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL void tp_compose_tiling_abandon(TpComposeTiling* tiling)
{
    if (tiling->open) tp_compose_reader_abandon(&tiling->reader);
    tiling->open = 0;
    tiling->failed = 1;
}

/* `{"bytes":B,"offset":O,"path":"P","sha256":"S"}` or `null`. expected is 1
 * (an artifact is required), 0 (null is required) or -1 (either). */
BUSTER_GLOBAL_LOCAL int tp_compose_metrics_field(char const* line, size_t length, int expected, TpComposeTiling* tiling,
                                                 unsigned members, uint64_t* intervals, uint64_t* memory)
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
        valid = cursor.valid && cursor.offset == count &&
                tp_compose_tiling_add(tiling, path, offset, bytes, sha256, members, intervals, memory);
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
    Arena* arena;
    TpComposeFamily family;
    TpRetirementComposeBounds bounds;
    TpComposeTiling timed, untimed_tiling;
    unsigned pairs, partitions, retained_files;
    uint64_t per_unit;
    TpComposeShard* shards;
    TpComposeShard* transcript;
    TpComposeShard* samples[2];
    double* ratios[TP_RETIREMENT_COMPOSE_METRICS];
    /* Sample-phase observations per (unit, round, pair, variant). */
    uint64_t* observations[TP_COMPOSE_OBSERVATIONS];
    uint64_t* member_intervals;
    uint64_t* member_memory;
    char* line;
    char* metrics_line;
    /* The store files at inventory time (before any composer output),
     * sorted by path, and each file's class and declaration. */
    unsigned files;
    unsigned* by_path;
    unsigned char* classes;
    unsigned* declared;
    Sha256 raw;
    uint64_t invocations, untimed_records, untimed_production, code_records;
    uint64_t input_bytes[TP_RETIREMENT_COMPOSE_PARTITIONS];
    unsigned partition_population[TP_RETIREMENT_COMPOSE_PARTITIONS];
    unsigned partition_first[TP_RETIREMENT_COMPOSE_PARTITIONS], partition_shards[TP_RETIREMENT_COMPOSE_PARTITIONS];
    TpRetirementComposePartition const* partition_plan[TP_RETIREMENT_COMPOSE_PARTITIONS];
    char raw_sha256[65], context_sha256[65];
    char summary[512];
    TpRetirementComposeArtifact manifests[TP_RETIREMENT_COMPOSE_PARTITIONS];
    TpRetirementComposeArtifact code, series, replay, receipt, bundle, sealed, untimed, retained;
    unsigned seal_entries;
    /* (#1880) Each ratio line's length, the planned series shard sizes and
     * the sealed shards; series is their manifest. */
    unsigned char* ratio_lines[TP_RETIREMENT_COMPOSE_METRICS];
    uint64_t* series_sizes;
    TpRetirementComposeArtifact* series_shards;
    unsigned series_shard_count;
    uint64_t series_bytes;
    char series_sha256[65];
} TpComposeState;

/* The store file at `path` (binary search of the sorted order), or NONE. */
BUSTER_GLOBAL_LOCAL unsigned tp_compose_find(TpComposeState const* state, char const* path)
{
    unsigned low = 0, high = state->files, found = TP_COMPOSE_NONE;
    while (path && low < high && found == TP_COMPOSE_NONE)
    {
        unsigned middle = low + (high - low) / 2;
        int order = strcmp(state->store->files[state->by_path[middle]].path, path);
        if (!order) found = state->by_path[middle];
        else if (order < 0) low = middle + 1;
        else high = middle;
    }
    return found;
}

/* Mark one declared sealed input: present once and not otherwise claimed. */
BUSTER_GLOBAL_LOCAL int tp_compose_claim(TpComposeState* state, char const* path)
{
    unsigned index = tp_compose_find(state, path);
    int valid = index != TP_COMPOSE_NONE && state->classes[index] == TP_COMPOSE_CLASS_FREE;
    if (valid) state->classes[index] = TP_COMPOSE_CLASS_SEALED;
    return valid;
}

/* Whether `path` is `<prefix>NNNN<suffix>`, and its index. */
BUSTER_GLOBAL_LOCAL int tp_compose_group_member(TpRetirementComposeRetained const* entry, char const* path,
                                                unsigned* index)
{
    size_t length = strlen(path), prefix = strlen(entry->prefix), suffix = strlen(entry->suffix);
    int valid = length == prefix + TP_COMPOSE_GROUP_DIGITS + suffix && !memcmp(path, entry->prefix, prefix) &&
                !memcmp(path + prefix + TP_COMPOSE_GROUP_DIGITS, entry->suffix, suffix);
    unsigned value = 0;
    for (unsigned i = 0; valid && i < TP_COMPOSE_GROUP_DIGITS; ++i)
    {
        char c = path[prefix + i];
        valid = c >= '0' && c <= '9';
        value = value * 10 + (unsigned)(c - '0');
    }
    *index = valid ? value : 0;
    return valid;
}

/* Every store file is exactly one declared sealed input or matches exactly
 * one entry of the retained declaration bound at plan time; every exact
 * retained file is present, every group is contiguous from 0000 within its
 * cap, and unreserved kinds stay within their reserved bytes. */
BUSTER_GLOBAL_LOCAL int tp_compose_inventory(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    TpRetirementComposeDeclaration const* declaration = request->declaration;
    TpRetirementStore const* store = state->store;
    int valid = 1;
    for (unsigned i = 0; valid && i < request->transcript_count; ++i) valid = tp_compose_claim(state, request->transcript_paths[i]);
    for (unsigned p = 0; p < 2; ++p)
        for (unsigned i = 0; valid && i < request->sample_counts[p]; ++i)
            valid = tp_compose_claim(state, request->sample_paths[p][i]);
    for (unsigned i = 0; valid && i < request->metrics_count; ++i) valid = tp_compose_claim(state, request->metrics_paths[i]);
    for (unsigned i = 0; valid && i < request->untimed_metrics_count; ++i)
        valid = tp_compose_claim(state, request->untimed_metrics_paths[i]);
    if (valid && request->untimed_path) valid = tp_compose_claim(state, request->untimed_path);
    unsigned counts[TP_RETIREMENT_COMPOSE_RETAINED_ENTRIES] = {0}, highest[TP_RETIREMENT_COMPOSE_RETAINED_ENTRIES] = {0};
    uint64_t bytes[TP_RETIREMENT_COMPOSE_RETAINED_ENTRIES] = {0};
    for (unsigned f = 0; valid && f < state->files; ++f)
    {
        TpRetirementStoredFile const* file = store->files + f;
        unsigned matches = 0, match = 0, index = 0;
        for (unsigned e = 0; state->classes[f] == TP_COMPOSE_CLASS_FREE && e < declaration->retained_count; ++e)
        {
            TpRetirementComposeRetained const* entry = declaration->retained + e;
            unsigned member = 0;
            int matched = entry->suffix ? tp_compose_group_member(entry, file->path, &member) :
                          !strcmp(entry->prefix, file->path);
            if (matched)
            {
                ++matches;
                match = e;
                index = member;
            }
        }
        if (state->classes[f] == TP_COMPOSE_CLASS_FREE)
        {
            valid = matches == 1;
            if (valid)
            {
                state->classes[f] = TP_COMPOSE_CLASS_RETAINED;
                state->declared[f] = match;
                ++counts[match];
                if (index > highest[match]) highest[match] = index;
                valid = tp_compose_add(&bytes[match], file->bytes);
                ++state->retained_files;
            }
        }
    }
    for (unsigned e = 0; valid && e < declaration->retained_count; ++e)
    {
        TpRetirementComposeRetained const* entry = declaration->retained + e;
        valid = entry->suffix ? counts[e] <= entry->files_max && (!counts[e] || highest[e] + 1 == counts[e]) :
                                counts[e] == 1;
        valid = valid && (entry->reserved || bytes[e] <= entry->bytes_max);
    }
    return valid;
}

/* Re-read one pre-existing file below the store root without following
 * links; require its size (bytes, or at most `maximum` when bytes is 0) and
 * digest; optionally keep its bytes in the arena. */
BUSTER_GLOBAL_LOCAL int tp_compose_evidence_file(int root, char const* path, uint64_t bytes, uint64_t maximum,
                                                 char const* sha256, Arena* arena, unsigned char** output,
                                                 uint64_t* output_bytes)
{
    int valid = root >= 0 && tp_compose_relative_path(path) && tp_compose_digest(sha256);
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
    valid = valid && fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
            (bytes ? (uint64_t)info.st_size == bytes : (uint64_t)info.st_size <= maximum);
    uint64_t size = valid ? (uint64_t)info.st_size : 0;
    unsigned char* kept = valid && output ? (unsigned char*)tp_retirement_compose_allocate(arena, size) : NULL;
    valid = valid && (!output || kept);
    Sha256 hash;
    sha256_init(&hash);
    uint64_t used = 0;
    unsigned char buffer[TP_COMPOSE_READ_BYTES];
    while (valid && used < size)
    {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        valid = count > 0 && (uint64_t)count <= size - used;
        if (valid)
        {
            sha256_add(&hash, buffer, (u64)count);
            if (kept) memcpy(kept + used, buffer, (size_t)count);
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
    if (output) *output = valid ? kept : NULL;
    if (output_bytes) *output_bytes = valid ? size : 0;
    return valid;
}

/* Rehash the pre-sample closure below the store root (none is a store file;
 * the declared entry count and bytes are exact) and join its plan, post-A/A
 * and result-input plan entries to the identities the receipt, bundle and
 * sealed record bind. */
BUSTER_GLOBAL_LOCAL int tp_compose_prior(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    unsigned plan = 0, post = 0, result_plan = 0;
    uint64_t total = 0;
    int valid = request->prior_count && request->prior && request->prior_count == request->declaration->prior_entries;
    for (unsigned i = 0; valid && i < request->prior_count; ++i)
    {
        TpRetirementComposeClosure const* entry = request->prior + i;
        valid = tp_compose_printable(entry->name, TP_RETIREMENT_COMPOSE_NAME_BYTES) && entry->bytes &&
                tp_compose_find(state, entry->path) == TP_COMPOSE_NONE &&
                tp_compose_evidence_file(state->store->root, entry->path, entry->bytes, 0, entry->sha256, NULL, NULL,
                                         NULL) &&
                tp_compose_add(&total, entry->bytes);
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
    valid = valid && plan == 1 && post == 1 && result_plan == 1 && total == request->declaration->prior_bytes;
    return valid;
}

/* The observation slot of (unit, round, pair, variant) in a table. */
BUSTER_GLOBAL_LOCAL size_t tp_compose_slot(TpComposeState const* state, unsigned unit, unsigned round, unsigned pair,
                                           unsigned variant)
{
    size_t slot = (((size_t)unit * state->per_unit) + (size_t)round * state->pairs + pair) * 2 + variant;
    return slot;
}

/* The dense timed row with census id `id`, or NONE. */
BUSTER_GLOBAL_LOCAL unsigned tp_compose_dense(TpRetirementComposeLayout const* layout, uint64_t id)
{
    unsigned low = 0, high = layout->row_count, found = TP_COMPOSE_NONE;
    while (low < high && found == TP_COMPOSE_NONE)
    {
        unsigned middle = low + (high - low) / 2;
        if (layout->rows[middle].id == id) found = middle;
        else if (layout->rows[middle].id < id) low = middle + 1;
        else high = middle;
    }
    return found;
}

/* Record one sample-phase invocation's observations: a runtime row's wall
 * time, a singleton row's wall time and RSS, or an object batch's wall time
 * and RSS plus each member's metrics interval and arena bytes. */
BUSTER_GLOBAL_LOCAL int tp_compose_observe(TpComposeState* state, TpRetirementInvocation const* invocation,
                                           uint64_t elapsed, uint64_t rss)
{
    TpComposeFamily const* family = &state->family;
    unsigned round = (unsigned)invocation->round, pair = (unsigned)invocation->pair, variant = invocation->variant;
    int valid = 1;
    if (invocation->kind)
    {
        unsigned dense = tp_compose_dense(state->request->layout, invocation->row);
        unsigned runtime = dense != TP_COMPOSE_NONE ? family->runtime_index[dense] : TP_COMPOSE_NONE;
        valid = runtime != TP_COMPOSE_NONE;
        if (valid) state->observations[TP_COMPOSE_RUNTIME][tp_compose_slot(state, runtime, round, pair, variant)] = elapsed;
    }
    else if (family->group_object[invocation->group] == TP_COMPOSE_NONE)
    {
        size_t slot = tp_compose_slot(state, family->group_first[invocation->group], round, pair, variant);
        state->observations[TP_COMPOSE_ROW_WALL][slot] = elapsed;
        state->observations[TP_COMPOSE_ROW_MEMORY][slot] = rss;
    }
    else
    {
        size_t slot = tp_compose_slot(state, family->group_object[invocation->group], round, pair, variant);
        state->observations[TP_COMPOSE_BATCH_WALL][slot] = elapsed;
        state->observations[TP_COMPOSE_BATCH_RSS][slot] = rss;
        for (unsigned i = family->group_offset[invocation->group]; i < family->group_offset[invocation->group + 1]; ++i)
        {
            unsigned member = i - family->group_offset[invocation->group];
            size_t row = tp_compose_slot(state, family->group_rows[i], round, pair, variant);
            state->observations[TP_COMPOSE_ROW_WALL][row] = state->member_intervals[member];
            state->observations[TP_COMPOSE_ROW_MEMORY][row] = state->member_memory[member];
        }
    }
    return valid;
}

/* One transcript line against the next #619 cursor invocation. */
BUSTER_GLOBAL_LOCAL int tp_compose_invocation(TpComposeState* state, TpRetirementExecution* execution, char const* line,
                                              size_t length, uint64_t* last_end)
{
    TpRetirementComposeRequest const* request = state->request;
    TpComposeFamily const* family = &state->family;
    TpRetirementInvocation expected = {0};
    uint64_t pid = 0, started = 0, finished = 0, sequence = 0, rss = 0;
    char token[TP_RETIREMENT_STORE_TOKEN_CAPACITY], instance[65], computed[65], seconds[32];
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
        started > *last_end && finished > started && finished < request->completed_at_ns &&
        tp_retirement_seconds(seconds, finished - started) &&
        tp_compose_field_is(line, length, "wall_seconds", seconds);
    /* A compiler process reports its peak RSS; a runtime process none. */
    if (valid && !expected.kind) valid = tp_compose_field_u64(line, length, "peak_rss_bytes", &rss) && rss &&
                                         rss <= TP_COMPOSE_MEMORY_MAX;
    else if (valid) valid = tp_compose_field_is(line, length, "peak_rss_bytes", "null");
    int object = valid && !expected.kind && family->group_object[expected.group] != TP_COMPOSE_NONE;
    unsigned members = object ? family->group_offset[expected.group + 1] - family->group_offset[expected.group] : 0;
    valid = valid && tp_compose_metrics_field(line, length, object, &state->timed, members, state->member_intervals,
                                              state->member_memory);
    /* Members run serially inside their batch's supervised interval. */
    uint64_t total = 0;
    for (unsigned i = 0; valid && i < members; ++i)
    {
        valid = state->member_intervals[i] <= finished - started - total;
        if (valid) total += state->member_intervals[i];
    }
    if (valid && expected.phase) valid = tp_compose_observe(state, &expected, finished - started, rss);
    if (valid)
    {
        *last_end = finished;
        valid = tp_retirement_execution_commit(execution, 1);
    }
    return valid;
}

/* The complete A/B transcript: D's cursor replays the frozen seeded schedule,
 * every process instance binds the campaign identity, intervals stay ordered
 * inside the bound window, every metrics artifact tiles its shard, and every
 * sample-phase observation is recorded for the numeric join. */
BUSTER_GLOBAL_LOCAL int tp_compose_transcript(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    TpRetirementComposeLayout const* layout = request->layout;
    TpComposeFamily const* family = &state->family;
    uint64_t slots = (uint64_t)layout->group_count * 3 + family->runtime_count;
    unsigned* workspace = (unsigned*)tp_retirement_compose_allocate(state->arena, slots * sizeof(unsigned));
    TpRetirementExecution execution;
    int valid = workspace && request->transcript_paths && request->transcript_count &&
        request->transcript_count <= TP_RETIREMENT_TRANSCRIPT_SHARDS &&
        tp_retirement_execution_init(&execution, request->statistics->seed, layout->group_count, family->runtime_ids,
            family->runtime_count, layout->population_rows, state->pairs, workspace, (size_t)slots);
    state->invocations = valid ? execution.expected : 0;
    uint64_t last_end = request->bound_at_ns;
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
            int read = tp_compose_reader_line(&reader, state->line, TP_COMPOSE_LINE_BYTES, &length);
            if (!read) more = 0;
            else valid = read == 1 && reader.lines <= TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS &&
                         tp_compose_invocation(state, &execution, state->line, length, &last_end);
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
    return valid;
}

/* Untimed code-artifact batch records: strictly ordered (group, variant,
 * purpose), each a fresh campaign-bound process outside the timed window,
 * one reproduction per group and variant, metrics tiling the untimed shards.
 * Production records are counted. */
BUSTER_GLOBAL_LOCAL int tp_compose_untimed(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    unsigned groups = request->layout->untimed_groups;
    int valid = groups ? request->untimed_path != NULL : !request->untimed_path && !request->untimed_metrics_count;
    unsigned char* reproduced = valid && groups ?
        (unsigned char*)tp_retirement_compose_allocate(state->arena, (uint64_t)groups * 2) : NULL;
    valid = valid && (!groups || reproduced);
    TpComposeReader reader = {0};
    if (valid && groups) valid = tp_compose_reader_open(&reader, state->store, request->untimed_path);
    uint64_t last_end = 0, last_key = UINT64_MAX;
    int more = valid && groups;
    while (valid && more)
    {
        size_t length = 0;
        int read = tp_compose_reader_line(&reader, state->line, TP_COMPOSE_LINE_BYTES, &length);
        char const* line = state->line;
        uint64_t group = 0, exit_status = 0, pid = 0, started = 0, finished = 0;
        char token[TP_RETIREMENT_STORE_TOKEN_CAPACITY], instance[65], computed[65], digest[65];
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
                tp_compose_metrics_field(line, length, -1, &state->untimed_tiling, 0, NULL, NULL);
            uint64_t key = (group * 2 + (uint64_t)candidate) * 2 + (uint64_t)reproduction;
            valid = valid && (last_key == UINT64_MAX || key > last_key);
            if (valid)
            {
                last_key = key;
                last_end = finished;
                state->untimed_production += (uint64_t)production;
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
    return valid;
}

/* The A/A stage's retained metrics shards are exactly what its transcript
 * references: the retained `retirement-metrics-<tag>-NNNN.txt` files, in
 * index order, tiled completely by the A/A transcripts' artifacts (a shard
 * that no artifact reaches, such as a trailing unpublished one, is refused).
 * The A/A transcripts must be retained files; the tag must differ from the
 * A/B and untimed writers'. */
BUSTER_GLOBAL_LOCAL int tp_compose_aa(TpComposeState* state, char const* timed_tag, char const* untimed_tag)
{
    TpRetirementComposeRequest const* request = state->request;
    char const* tag = request->aa_metrics_tag;
    unsigned count = request->aa_transcript_count;
    size_t tag_length = tag ? strnlen(tag, TP_RETIREMENT_METRICS_TAG_BYTES + 1) : 0;
    int valid = count <= TP_RETIREMENT_TRANSCRIPT_SHARDS && (!count || request->aa_transcript_paths) &&
                (!tag || (tag_length && tag_length <= TP_RETIREMENT_METRICS_TAG_BYTES && strcmp(tag, timed_tag) &&
                          strcmp(tag, untimed_tag) && strcmp(tag, TP_RETIREMENT_UNTIMED_METRICS_TAG)));
    char const** paths = valid ? (char const**)tp_retirement_compose_allocate(state->arena,
                             (uint64_t)state->files * sizeof(char const*)) : NULL;
    unsigned shards = 0;
    valid = valid && paths;
    for (unsigned i = 0; valid && tag && i < state->files; ++i)
    {
        unsigned index = state->by_path[i];
        char shard_tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1];
        unsigned ordinal = 0;
        if (state->classes[index] == TP_COMPOSE_CLASS_RETAINED &&
            tp_compose_shard_tag(state->store->files[index].path, shard_tag, &ordinal) && !strcmp(shard_tag, tag))
            paths[shards++] = state->store->files[index].path;
    }
    TpComposeTiling tiling = {0};
    char found[TP_RETIREMENT_METRICS_TAG_BYTES + 1];
    valid = valid && tp_compose_tiling_init(&tiling, state->store, paths, shards, state->metrics_line, found);
    for (unsigned s = 0; valid && s < count; ++s)
    {
        char const* path = request->aa_transcript_paths[s];
        unsigned index = tp_compose_find(state, path);
        TpComposeReader reader = {0};
        valid = index != TP_COMPOSE_NONE && state->classes[index] == TP_COMPOSE_CLASS_RETAINED &&
                tp_compose_reader_open(&reader, state->store, path);
        int more = valid;
        while (valid && more)
        {
            size_t length = 0;
            int read = tp_compose_reader_line(&reader, state->line, TP_COMPOSE_LINE_BYTES, &length);
            if (!read) more = 0;
            else valid = read == 1 && reader.lines <= TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS &&
                         tp_compose_metrics_field(state->line, length, tag ? -1 : 0, &tiling, 0, NULL, NULL);
        }
        uint64_t records = reader.lines;
        if (valid) valid = tp_compose_reader_close(&reader) && records;
        else tp_compose_reader_abandon(&reader);
    }
    if (valid) valid = tp_compose_tiling_finish(&tiling);
    else tp_compose_tiling_abandon(&tiling);
    return valid;
}

/* A candidate/baseline ratio exactly as the validator recomputes it. */
BUSTER_GLOBAL_LOCAL int tp_compose_ratio(double candidate, double baseline, double* output)
{
    double ratio = candidate / baseline;
    int valid = isfinite(ratio) && ratio > 0.0;
    *output = valid ? ratio : 0.0;
    return valid;
}

/* A sample's seconds text is D's encoding of its observed nanoseconds. */
BUSTER_GLOBAL_LOCAL int tp_compose_seconds_are(char const* text, size_t length, uint64_t nanoseconds)
{
    char expected[32];
    int valid = nanoseconds && tp_retirement_seconds(expected, nanoseconds) && strlen(expected) == length &&
                !memcmp(expected, text, length);
    return valid;
}

/* One `row-round-pair` record of D's tp_retirement_sample_record, at its
 * frozen coordinate, joined to its observations; its ratios are captured. */
BUSTER_GLOBAL_LOCAL int tp_compose_row_record(TpComposeState* state, char const* line, size_t length, uint64_t ordinal)
{
    unsigned pairs = state->pairs;
    uint64_t per_unit = state->per_unit;
    unsigned unit = (unsigned)(ordinal / per_unit), round = (unsigned)(ordinal / pairs % TP_RETIREMENT_ROUNDS);
    unsigned pair = (unsigned)(ordinal % pairs);
    TpRetirementComposeRow const* row = state->request->layout->rows + unit;
    unsigned runtime_unit = row->runtime ? state->family.runtime_index[unit] : 0;
    uint64_t memory[2] = {0, 0};
    double wall[2], runtime[2] = {1.0, 1.0};
    char const* wall_text[2];
    char const* runtime_text[2] = {NULL, NULL};
    size_t wall_length[2], runtime_length[2] = {0, 0};
    uint64_t pair_field = 0, id = 0, round_id = 0, pair_id = 0, round_field = 0, row_field = 0;
    TpComposeCursor cursor = {line, length, 0, 1};
    tp_compose_cursor_literal(&cursor, "{\"measurements\":{\"compiler_peak_memory\":{\"baseline\":");
    tp_compose_cursor_u64(&cursor, &memory[0]);
    tp_compose_cursor_literal(&cursor, ",\"candidate\":");
    tp_compose_cursor_u64(&cursor, &memory[1]);
    tp_compose_cursor_literal(&cursor, "},\"compiler_wall_time\":{\"baseline\":");
    tp_compose_cursor_number(&cursor, &wall_text[0], &wall_length[0], &wall[0]);
    tp_compose_cursor_literal(&cursor, ",\"candidate\":");
    tp_compose_cursor_number(&cursor, &wall_text[1], &wall_length[1], &wall[1]);
    tp_compose_cursor_literal(&cursor, "}");
    if (row->runtime)
    {
        tp_compose_cursor_literal(&cursor, ",\"generated_runtime\":{\"baseline\":");
        tp_compose_cursor_number(&cursor, &runtime_text[0], &runtime_length[0], &runtime[0]);
        tp_compose_cursor_literal(&cursor, ",\"candidate\":");
        tp_compose_cursor_number(&cursor, &runtime_text[1], &runtime_length[1], &runtime[1]);
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
                round_id == round && round_field == round && id == row->id && row_field == row->id;
    for (unsigned variant = 0; valid && variant < 2; ++variant)
    {
        size_t slot = tp_compose_slot(state, unit, round, pair, variant);
        valid = tp_compose_seconds_are(wall_text[variant], wall_length[variant],
                                       state->observations[TP_COMPOSE_ROW_WALL][slot]) &&
                memory[variant] && memory[variant] == state->observations[TP_COMPOSE_ROW_MEMORY][slot];
        if (valid && row->runtime)
            valid = tp_compose_seconds_are(runtime_text[variant], runtime_length[variant],
                state->observations[TP_COMPOSE_RUNTIME][tp_compose_slot(state, runtime_unit, round, pair, variant)]);
    }
    valid = valid && tp_compose_ratio(wall[1], wall[0], &state->ratios[0][index]) &&
            tp_compose_ratio((double)memory[1], (double)memory[0], &state->ratios[1][index]);
    if (valid && row->runtime)
        valid = tp_compose_ratio(runtime[1], runtime[0],
                                 &state->ratios[2][(size_t)runtime_unit * per_unit + (size_t)round * pairs + pair]);
    return valid;
}

/* One `group-round-pair` record of D's tp_retirement_batch_record, joined to
 * its batch's observations. */
BUSTER_GLOBAL_LOCAL int tp_compose_batch_record(TpComposeState* state, char const* line, size_t length, uint64_t ordinal)
{
    unsigned pairs = state->pairs;
    uint64_t per_unit = state->per_unit;
    unsigned unit = (unsigned)(ordinal / per_unit), round = (unsigned)(ordinal / pairs % TP_RETIREMENT_ROUNDS);
    unsigned pair = (unsigned)(ordinal % pairs);
    unsigned group = state->family.object_groups[unit];
    uint64_t rss[2] = {0, 0};
    double wall[2];
    char const* wall_text[2];
    size_t wall_length[2];
    uint64_t group_field = 0, pair_field = 0, group_id = 0, round_id = 0, pair_id = 0, round_field = 0;
    TpComposeCursor cursor = {line, length, 0, 1};
    tp_compose_cursor_literal(&cursor, "{\"group\":");
    tp_compose_cursor_u64(&cursor, &group_field);
    tp_compose_cursor_literal(&cursor, ",\"measurements\":{\"compiler_batch_peak_rss\":{\"baseline\":");
    tp_compose_cursor_u64(&cursor, &rss[0]);
    tp_compose_cursor_literal(&cursor, ",\"candidate\":");
    tp_compose_cursor_u64(&cursor, &rss[1]);
    tp_compose_cursor_literal(&cursor, "},\"compiler_batch_wall_time\":{\"baseline\":");
    tp_compose_cursor_number(&cursor, &wall_text[0], &wall_length[0], &wall[0]);
    tp_compose_cursor_literal(&cursor, ",\"candidate\":");
    tp_compose_cursor_number(&cursor, &wall_text[1], &wall_length[1], &wall[1]);
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
                pair_field == pair && pair_id == pair && round_id == round && round_field == round;
    for (unsigned variant = 0; valid && variant < 2; ++variant)
    {
        size_t slot = tp_compose_slot(state, unit, round, pair, variant);
        valid = tp_compose_seconds_are(wall_text[variant], wall_length[variant],
                                       state->observations[TP_COMPOSE_BATCH_WALL][slot]) &&
                rss[variant] && rss[variant] == state->observations[TP_COMPOSE_BATCH_RSS][slot];
    }
    valid = valid && tp_compose_ratio(wall[1], wall[0], &state->ratios[3][index]) &&
            tp_compose_ratio((double)rss[1], (double)rss[0], &state->ratios[4][index]);
    return valid;
}

/* Both #615 populations, in canonical record order: every record present
 * once at its frozen coordinate and equal to its transcript observation,
 * full-size shards before the last, and the streamed bytes (rows, then
 * batches) are the raw measurement digest. */
BUSTER_GLOBAL_LOCAL int tp_compose_samples(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    uint64_t expected[2] = {(uint64_t)request->layout->row_count * state->per_unit,
                            (uint64_t)state->family.object_count * state->per_unit};
    int valid = 1;
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
                int read = tp_compose_reader_line(&reader, state->line, TP_RETIREMENT_SAMPLE_LINE_CAP, &length);
                if (!read) more = 0;
                else
                {
                    valid = read == 1 && ordinal < expected[p] &&
                            (p ? tp_compose_batch_record(state, state->line, length, ordinal) :
                                 tp_compose_row_record(state, state->line, length, ordinal));
                    if (valid)
                    {
                        sha256_add(&state->raw, state->line, (u64)length);
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
    return valid;
}

/* The pre-sample plan's partitions: contiguous full-cap #615 partitions per
 * population, unique identities and paths, and whole shards in each. */
BUSTER_GLOBAL_LOCAL int tp_compose_partitions(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    uint64_t expected[2] = {(uint64_t)request->layout->row_count * state->per_unit,
                            (uint64_t)state->family.object_count * state->per_unit};
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
                    !tp_compose_reserved_path(partition->path) && partition->start == start &&
                    partition->records == records && records && records <= TP_RETIREMENT_SAMPLE_PARTITION_RECORDS;
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

/* The post-sample execution context: the validator's _execution_context of
 * the authenticated post-A/A binding (below the store root, not a store
 * file) with the streamed numeric digest, in canonical JSON. Its post-A/A
 * phase digest must be the one the sealed record binds. */
BUSTER_GLOBAL_LOCAL int tp_compose_context(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    static char const* const post_path[] = {"workflow", "phases", "post_aa_binding", "sha256"};
    unsigned char* binding = NULL;
    uint64_t bytes = 0;
    char* context = NULL;
    size_t context_bytes = 0;
    TpComposeJson json = {0};
    int valid = request->binding_path && tp_compose_find(state, request->binding_path) == TP_COMPOSE_NONE &&
                tp_compose_evidence_file(state->store->root, request->binding_path, 0, TP_COMPOSE_BINDING_BYTES,
                                         request->binding_sha256, state->arena, &binding, &bytes) &&
                tp_retirement_compose_json_parse(binding, (size_t)bytes, state->arena, &json);
    unsigned node = valid ? 0u : TP_COMPOSE_JSON_NONE;
    for (unsigned i = 0; node != TP_COMPOSE_JSON_NONE && i < BUSTER_ARRAY_LENGTH(post_path); ++i)
        node = tp_retirement_compose_json_member(&json, node, post_path[i]);
    valid = valid && node != TP_COMPOSE_JSON_NONE && json.nodes[node].kind == TP_COMPOSE_JSON_STRING &&
            json.nodes[node].length == 64 && !memcmp(json.nodes[node].text, request->post_aa_binding_sha256, 64) &&
            tp_retirement_compose_execution_context(binding, (size_t)bytes, state->raw_sha256, state->arena, &context,
                                                    &context_bytes);
    if (valid) tp_compose_sha_hex(context, context_bytes, state->context_sha256);
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

BUSTER_GLOBAL_LOCAL void tp_compose_writer_begin(TpComposeWriter* writer, TpRetirementStore* store, char const* path,
                                                 uint64_t limit, FILE* copy)
{
    *writer = (TpComposeWriter){.store = store, .copy = copy, .limit = limit};
    sha256_init(&writer->hash);
    writer->valid = limit && limit <= TP_RETIREMENT_STORE_FILE_BYTES &&
                    tp_retirement_store_begin(store, path, limit, &writer->pending);
}

BUSTER_GLOBAL_LOCAL void tp_compose_writer_bytes(TpComposeWriter* writer, void const* data, size_t length)
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

BUSTER_GLOBAL_LOCAL void tp_compose_writer_format(TpComposeWriter* writer, char const* format, ...)
{
    char buffer[2048];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    writer->valid = writer->valid && length >= 0 && (size_t)length < sizeof(buffer);
    if (writer->valid) tp_compose_writer_bytes(writer, buffer, (size_t)length);
}

BUSTER_GLOBAL_LOCAL int tp_compose_writer_publish(TpComposeWriter* writer, TpRetirementComposeArtifact* artifact)
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
BUSTER_GLOBAL_LOCAL int tp_compose_manifests(TpComposeState* state)
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
BUSTER_GLOBAL_LOCAL int tp_compose_within_one_percent(uint64_t candidate, uint64_t baseline)
{
    int within = candidate <= baseline || candidate - baseline <= baseline / 100;
    return within;
}

/* The code-byte record set through D's encoder, and the exact summary the
 * validator's _code_bytes_summary derives from it. */
BUSTER_GLOBAL_LOCAL int tp_compose_code(TpComposeState* state)
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
            char text[TP_RETIREMENT_COMPOSE_REPR_BYTES], item[96];
            double ratio = (double)candidate / (double)baseline;
            valid = tp_compose_add(&baseline_total, baseline) && tp_compose_add(&candidate_total, candidate) &&
                    baseline_total <= TP_COMPOSE_TOTAL_BYTES_MAX && candidate_total <= TP_COMPOSE_TOTAL_BYTES_MAX &&
                    tp_retirement_compose_float_repr(ratio, text);
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
    char aggregate_text[TP_RETIREMENT_COMPOSE_REPR_BYTES], maximum_text[TP_RETIREMENT_COMPOSE_REPR_BYTES];
    char ratios_sha256[65];
    valid = valid && tp_compose_decimal_ratio(candidate_total, baseline_total, &aggregate) &&
            tp_retirement_compose_float_repr(aggregate, aggregate_text) &&
            tp_retirement_compose_float_repr(maximum, maximum_text);
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

/* (#1880) The #619 statistics input the reviewed adapter consumes: every
 * family member in sorted order with its cells' ratios, round-major, split
 * greedily over whole lines into series shards of at most the shard size.
 * One pass (TpComposeSeries) either plans the canonical shards before the
 * settle (counting, from each ratio line's precomputed length) or writes
 * them; the write must reproduce the planned shard sizes exactly. The same
 * bytes go to the store and to the adapter's private scratch copies. */
typedef struct TpComposeSeries
{
    TpComposeState* state;
    TpComposeWriter writer;
    FILE* copy;
    Sha256 total;
    uint64_t shard_bytes, used, total_bytes;
    unsigned shard, writing, open;
    int valid;
} TpComposeSeries;

BUSTER_GLOBAL_LOCAL void tp_compose_series_shard_path(char path[TP_RETIREMENT_STORE_PATH_BYTES + 1], unsigned shard)
{
    snprintf(path, TP_RETIREMENT_STORE_PATH_BYTES + 1, "%s%04u%s", TP_RETIREMENT_COMPOSE_SERIES_SHARD_PREFIX, shard,
             TP_RETIREMENT_COMPOSE_SERIES_SHARD_SUFFIX);
}

/* Close the open shard: planning records its size, writing seals it and
 * requires the planned size. */
BUSTER_GLOBAL_LOCAL void tp_compose_series_close(TpComposeSeries* series)
{
    TpComposeState* state = series->state;
    if (series->writing)
    {
        TpRetirementComposeArtifact* artifact = state->series_shards + series->shard;
        int valid = series->valid && series->used == state->series_sizes[series->shard];
        if (!valid) series->writer.valid = 0;
        valid = tp_compose_writer_publish(&series->writer, artifact) && valid;
        if (series->copy && fclose(series->copy) != 0) valid = 0;
        series->copy = NULL;
        series->valid = valid;
    }
    else state->series_sizes[series->shard] = series->used;
    series->open = 0;
    ++series->shard;
}

/* Append one series line of `length` bytes, opening the next shard where it
 * would not fit; planning passes no text. */
BUSTER_GLOBAL_LOCAL void tp_compose_series_line(TpComposeSeries* series, char const* text, size_t length)
{
    TpComposeState* state = series->state;
    series->valid = series->valid && length && length <= TP_COMPOSE_SERIES_LINE_BYTES &&
                    (!series->writing || (text && text[length - 1] == '\n'));
    if (series->valid && series->open && series->used + length > series->shard_bytes) tp_compose_series_close(series);
    if (series->valid && !series->open)
    {
        series->valid = series->shard < (series->writing ? state->series_shard_count : state->bounds.series_shards);
        series->used = 0;
        series->open = 1;
        if (series->valid && series->writing)
        {
            char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
            tp_compose_series_shard_path(path, series->shard);
            int fd = openat(state->request->scratch_root, path, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC,
                            0600);
            series->copy = fd >= 0 ? fdopen(fd, "wb") : NULL;
            if (fd >= 0 && !series->copy) close(fd);
            tp_compose_writer_begin(&series->writer, state->store, path, state->series_sizes[series->shard],
                                    series->copy);
            series->valid = series->writer.valid && series->copy;
            if (!series->valid) series->open = 0;
        }
    }
    if (series->valid && series->writing)
    {
        tp_compose_writer_bytes(&series->writer, text, length);
        series->valid = series->writer.valid;
        if (series->valid) sha256_add(&series->total, text, (u64)length);
    }
    if (series->valid)
    {
        series->used += length;
        series->total_bytes += length;
    }
}

BUSTER_GLOBAL_LOCAL void tp_compose_series_format(TpComposeSeries* series, char const* format, ...)
{
    char buffer[TP_COMPOSE_SERIES_LINE_BYTES + 1];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    series->valid = series->valid && length > 0 && (size_t)length < sizeof(buffer);
    if (series->valid) tp_compose_series_line(series, buffer, (size_t)length);
}

/* One pass over the series. Planning takes every ratio line's length from
 * state->ratio_lines; writing formats the line and requires that length. */
BUSTER_GLOBAL_LOCAL int tp_compose_series_pass(TpComposeState* state, unsigned writing)
{
    TpRetirementComposeRequest const* request = state->request;
    TpComposeFamily const* family = &state->family;
    TpRetirementPlan const* plan = request->statistics;
    uint64_t per_unit = state->per_unit;
    TpComposeSeries series = {.state = state, .shard_bytes = tp_compose_series_shard_bytes(), .writing = writing,
                              .valid = 1};
    sha256_init(&series.total);
    tp_compose_series_format(&series, "version=%u seed=%" PRIu64 " bootstrap_members=%u cell_members=%u pairs=%u "
        "resamples=%u frozen=1 members=%u\n", plan->version, plan->seed, family->bootstrap, family->cells_total,
        state->pairs, plan->resamples, family->count);
    for (unsigned i = 0; series.valid && i < family->count; ++i)
    {
        TpComposeMember const* member = family->members + family->order[i];
        tp_compose_series_format(&series, "member=%s metric=%u kind=%u family=%u cells=%u pairs=%u resamples=%u limit=%s\n",
            member->name, member->metric, member->kind, member->family, member->cells, state->pairs,
            member->kind ? 0u : plan->resamples,
            member->kind ? tp_compose_cell_limits[member->metric] : tp_compose_aggregate_limits[member->metric]);
        unsigned written = 0;
        for (unsigned cell = 0; series.valid && cell < family->cells[member->metric]; ++cell)
        {
            int selected = member->kind ? cell == member->unit : member->dimension == TP_COMPOSE_NONE ||
                !strcmp(tp_compose_cell_value(family, member->metric, cell, member->dimension), member->value);
            for (uint64_t index = 0; selected && series.valid && index < per_unit; ++index)
            {
                uint64_t slot = cell * per_unit + index;
                unsigned length = state->ratio_lines[member->metric][slot];
                if (writing)
                {
                    char line[TP_COMPOSE_RATIO_LINE_BYTES + 1];
                    int formatted = snprintf(line, sizeof(line), "ratio=%.17g\n", state->ratios[member->metric][slot]);
                    series.valid = formatted > 0 && (unsigned)formatted == length;
                    if (series.valid) tp_compose_series_line(&series, line, length);
                }
                else tp_compose_series_line(&series, NULL, length);
            }
            written += (unsigned)selected;
        }
        series.valid = series.valid && written == member->cells;
        tp_compose_series_format(&series, "end\n");
    }
    /* The final shard. Planning fixes the exact shard count and bytes, which
     * writing must reproduce; writing also derives the series digest. */
    if (series.valid && series.open) tp_compose_series_close(&series);
    if (series.valid && !writing)
    {
        state->series_shard_count = series.shard;
        state->series_bytes = series.total_bytes;
    }
    series.valid = series.valid && series.shard == state->series_shard_count &&
                   series.total_bytes == state->series_bytes;
    if (series.valid && writing) sha256_finish_hex(&series.total, (char8*)state->series_sha256);
    if (series.open && writing)
    {
        series.writer.valid = 0;
        tp_compose_writer_publish(&series.writer, NULL);
    }
    if (series.copy) fclose(series.copy);
    return series.valid;
}

/* Before the settle: every ratio line's length (formatted once), then the
 * canonical shard plan. */
BUSTER_GLOBAL_LOCAL int tp_compose_series_plan(TpComposeState* state)
{
    TpComposeFamily const* family = &state->family;
    state->series_sizes = (uint64_t*)tp_retirement_compose_allocate(state->arena,
                              (uint64_t)state->bounds.series_shards * sizeof(uint64_t));
    state->series_shards = (TpRetirementComposeArtifact*)tp_retirement_compose_allocate(state->arena,
                               (uint64_t)state->bounds.series_shards * sizeof(TpRetirementComposeArtifact));
    int valid = state->series_sizes && state->series_shards;
    for (unsigned m = 0; valid && m < TP_RETIREMENT_COMPOSE_METRICS; ++m)
    {
        uint64_t count = (uint64_t)family->cells[m] * state->per_unit;
        state->ratio_lines[m] = (unsigned char*)tp_retirement_compose_allocate(state->arena, count);
        valid = state->ratio_lines[m] != NULL;
        for (uint64_t slot = 0; valid && slot < count; ++slot)
        {
            char line[TP_COMPOSE_RATIO_LINE_BYTES + 1];
            int length = snprintf(line, sizeof(line), "ratio=%.17g\n", state->ratios[m][slot]);
            valid = length > 0 && (unsigned)length <= TP_COMPOSE_RATIO_LINE_BYTES;
            if (valid) state->ratio_lines[m][slot] = (unsigned char)length;
        }
    }
    valid = valid && tp_compose_series_pass(state, 0);
    return valid;
}

/* Write the planned shards, then the manifest the adapter and the validator
 * read (tp_retirement_compose.h, TP_RETIREMENT_COMPOSE_SERIES_PATH). */
BUSTER_GLOBAL_LOCAL int tp_compose_series(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    int valid = tp_compose_series_pass(state, 1);
    int fd = valid ? openat(request->scratch_root, TP_RETIREMENT_COMPOSE_SERIES_PATH,
                            O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600) : -1;
    FILE* copy = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && !copy) close(fd);
    TpComposeWriter writer;
    if (valid)
    {
        tp_compose_writer_begin(&writer, state->store, TP_RETIREMENT_COMPOSE_SERIES_PATH, state->bounds.series_manifest,
                                copy);
        writer.valid = writer.valid && copy;
        tp_compose_writer_format(&writer, "%sseries bytes=%" PRIu64 " sha256=%s shards=%u shard_bytes=%" PRIu64 "\n",
            TP_RETIREMENT_COMPOSE_SERIES_MANIFEST_HEADER, state->series_bytes, state->series_sha256,
            state->series_shard_count, tp_compose_series_shard_bytes());
        uint64_t offset = 0;
        for (unsigned s = 0; s < state->series_shard_count; ++s)
        {
            TpRetirementComposeArtifact const* shard = state->series_shards + s;
            tp_compose_writer_format(&writer, "shard=%u offset=%" PRIu64 " bytes=%" PRIu64 " sha256=%s path=%s\n", s,
                                     offset, shard->bytes, shard->sha256, shard->path);
            offset += shard->bytes;
        }
        writer.valid = writer.valid && offset == state->series_bytes;
        valid = tp_compose_writer_publish(&writer, &state->series);
    }
    if (copy && fclose(copy) != 0) valid = 0;
    return valid;
}

/* A JSON integer node equal to `expected`. */
BUSTER_GLOBAL_LOCAL int tp_compose_json_is(TpComposeJson const* json, unsigned node, uint64_t expected)
{
    char text[24];
    int length = snprintf(text, sizeof(text), "%" PRIu64, expected);
    int valid = node < json->count && json->nodes[node].kind == TP_COMPOSE_JSON_INTEGER &&
                json->nodes[node].length == (uint32_t)length && !memcmp(json->nodes[node].text, text, (size_t)length);
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_compose_json_numeric(TpComposeJson const* json, unsigned node, double* value)
{
    int valid = node < json->count && (json->nodes[node].kind == TP_COMPOSE_JSON_INTEGER ||
                                       json->nodes[node].kind == TP_COMPOSE_JSON_FLOAT) &&
                isfinite(json->nodes[node].number);
    *value = valid ? json->nodes[node].number : 0.0;
    return valid;
}

/* One {estimate, lower, upper} #619 bound, positive and ordered. */
BUSTER_GLOBAL_LOCAL int tp_compose_replay_bound(TpComposeJson const* json, unsigned node, double limit, int* passed,
                                                int* regressed)
{
    static char const* const fields[] = {"estimate", "lower", "upper"};
    double estimate = 0.0, lower = 0.0, upper = 0.0;
    int valid = tp_retirement_compose_json_keys(json, node, fields, BUSTER_ARRAY_LENGTH(fields)) &&
                tp_compose_json_numeric(json, tp_retirement_compose_json_member(json, node, "estimate"), &estimate) &&
                tp_compose_json_numeric(json, tp_retirement_compose_json_member(json, node, "lower"), &lower) &&
                tp_compose_json_numeric(json, tp_retirement_compose_json_member(json, node, "upper"), &upper) &&
                estimate > 0.0 && lower >= 0.0 && upper > 0.0 && lower <= upper;
    *passed = *passed && upper <= limit;
    *regressed = *regressed && lower > limit;
    return valid;
}

/* The adapter's own JSON, structurally: the approved schema, every family
 * member once in the series order with its #619 metric, kind and family
 * index, bootstrap resampling only for bootstrap members, the family's tail
 * alpha, both round bounds and the pooled bound, and the outcome those
 * bounds imply. Its numbers are replayed by the validator from the reviewed
 * source, never trusted here. */
BUSTER_GLOBAL_LOCAL int tp_compose_replay_check(TpComposeState* state, char const* bytes, size_t length)
{
    static char const* const top[] = {"members", "schema", "version"};
    static char const* const fields[] = {"member", "metric", "kind", "family_index", "outcome", "valid", "resampled",
                                         "resamples", "tail_alpha", "round", "pooled"};
    TpComposeFamily const* family = &state->family;
    TpRetirementPlan const* plan = state->request->statistics;
    TpComposeJson json = {0};
    int valid = tp_retirement_compose_json_parse((unsigned char const*)bytes, length, state->arena, &json) &&
                tp_retirement_compose_json_keys(&json, 0, top, BUSTER_ARRAY_LENGTH(top));
    unsigned schema = valid ? tp_retirement_compose_json_member(&json, 0, "schema") : TP_COMPOSE_JSON_NONE;
    unsigned members = valid ? tp_retirement_compose_json_member(&json, 0, "members") : TP_COMPOSE_JSON_NONE;
    valid = valid && json.nodes[schema].kind == TP_COMPOSE_JSON_STRING &&
            json.nodes[schema].length == strlen(TP_COMPOSE_REPLAY_SCHEMA) &&
            !memcmp(json.nodes[schema].text, TP_COMPOSE_REPLAY_SCHEMA, json.nodes[schema].length) &&
            tp_compose_json_is(&json, tp_retirement_compose_json_member(&json, 0, "version"), 1) &&
            json.nodes[members].kind == TP_COMPOSE_JSON_ARRAY && json.nodes[members].count == family->count;
    unsigned call = valid ? json.nodes[members].first : TP_COMPOSE_JSON_NONE;
    for (unsigned i = 0; valid && i < family->count; ++i, call = json.nodes[call].next)
    {
        TpComposeMember const* member = family->members + family->order[i];
        unsigned name = tp_retirement_compose_json_member(&json, call, "member");
        unsigned outcome = tp_retirement_compose_json_member(&json, call, "outcome");
        unsigned resampled = tp_retirement_compose_json_member(&json, call, "resampled");
        unsigned rounds = tp_retirement_compose_json_member(&json, call, "round");
        double alpha = 0.0, limit = strtod(member->kind ? tp_compose_cell_limits[member->metric] :
                                                          tp_compose_aggregate_limits[member->metric], NULL);
        unsigned count = member->kind ? plan->cell_members_per_scope : plan->bootstrap_members_per_scope;
        double expected_alpha = 0.05 / (2.0 * (double)TP_COMPOSE_SCOPES * 2.0 * (double)count);
        valid = tp_retirement_compose_json_keys(&json, call, fields, BUSTER_ARRAY_LENGTH(fields)) &&
                json.nodes[name].kind == TP_COMPOSE_JSON_STRING && json.nodes[name].length == strlen(member->name) &&
                !memcmp(json.nodes[name].text, member->name, json.nodes[name].length) &&
                tp_compose_json_is(&json, tp_retirement_compose_json_member(&json, call, "metric"), member->metric) &&
                tp_compose_json_is(&json, tp_retirement_compose_json_member(&json, call, "kind"), member->kind) &&
                tp_compose_json_is(&json, tp_retirement_compose_json_member(&json, call, "family_index"), member->family) &&
                json.nodes[tp_retirement_compose_json_member(&json, call, "valid")].kind == TP_COMPOSE_JSON_TRUE &&
                json.nodes[resampled].kind == (member->kind ? TP_COMPOSE_JSON_FALSE : TP_COMPOSE_JSON_TRUE) &&
                tp_compose_json_is(&json, tp_retirement_compose_json_member(&json, call, "resamples"),
                                   member->kind ? 0u : plan->resamples) &&
                tp_compose_json_numeric(&json, tp_retirement_compose_json_member(&json, call, "tail_alpha"), &alpha) &&
                alpha == expected_alpha && json.nodes[rounds].kind == TP_COMPOSE_JSON_ARRAY &&
                json.nodes[rounds].count == TP_COMPOSE_SCOPES - 1 && json.nodes[outcome].kind == TP_COMPOSE_JSON_STRING;
        int passed = 1, regressed = 1;
        for (unsigned bound = valid ? json.nodes[rounds].first : TP_COMPOSE_JSON_NONE;
             valid && bound != TP_COMPOSE_JSON_NONE; bound = json.nodes[bound].next)
            valid = tp_compose_replay_bound(&json, bound, limit, &passed, &regressed);
        valid = valid && tp_compose_replay_bound(&json, tp_retirement_compose_json_member(&json, call, "pooled"), limit,
                                                 &passed, &regressed);
        char const* derived = passed ? "pass" : regressed ? "regression" : "inconclusive";
        valid = valid && json.nodes[outcome].length == strlen(derived) &&
                !memcmp(json.nodes[outcome].text, derived, json.nodes[outcome].length);
    }
    return valid;
}

/* Execute the adapter from the descriptor whose bytes were hashed, with no
 * inherited descriptors, in the scratch directory, under a wall-clock
 * limit; the executable must be unchanged after it exits. */
BUSTER_GLOBAL_LOCAL int tp_compose_adapter_run(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    struct stat before = {0}, after = {0}, absent = {0};
    int fd = request->adapter_path && request->adapter_path[0] == '/' && tp_compose_digest(request->adapter_sha256) ?
             open(request->adapter_path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
    int valid = fd >= 0 && fstat(fd, &before) == 0 && S_ISREG(before.st_mode) && (before.st_mode & S_IXUSR) &&
                before.st_size > 0 && (uint64_t)before.st_size <= TP_COMPOSE_ADAPTER_BYTES &&
                request->scratch_root >= 0 &&
                fstatat(request->scratch_root, TP_RETIREMENT_COMPOSE_REPLAY_PATH, &absent, AT_SYMLINK_NOFOLLOW) != 0 &&
                errno == ENOENT;
    Sha256 hash;
    sha256_init(&hash);
    uint64_t used = 0;
    unsigned char buffer[TP_COMPOSE_READ_BYTES];
    while (valid && used < (uint64_t)before.st_size)
    {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        valid = count > 0 && (uint64_t)count <= (uint64_t)before.st_size - used;
        if (valid)
        {
            sha256_add(&hash, buffer, (u64)count);
            used += (uint64_t)count;
        }
    }
    if (valid)
    {
        char digest[65];
        sha256_finish_hex(&hash, (char8*)digest);
        valid = !strcmp(digest, request->adapter_sha256);
    }
    pid_t child = valid ? fork() : -1;
    if (child == 0)
    {
        char* const arguments[] = {(char*)request->adapter_path, (char*)"retirement-replay", (char*)"--input",
                                   (char*)TP_RETIREMENT_COMPOSE_SERIES_PATH, (char*)"--output",
                                   (char*)TP_RETIREMENT_COMPOSE_REPLAY_PATH, NULL};
        char* const environment[] = {(char*)"LC_ALL=C", NULL};
        if (fchdir(request->scratch_root) == 0)
        {
#if defined(SYS_close_range) && defined(CLOSE_RANGE_CLOEXEC)
            syscall(SYS_close_range, 3u, ~0u, (unsigned)CLOSE_RANGE_CLOEXEC);
#endif
            fexecve(fd, arguments, environment);
        }
        _exit(127);
    }
    uint64_t limit = request->adapter_timeout_ns ? request->adapter_timeout_ns : TP_RETIREMENT_COMPOSE_ADAPTER_TIMEOUT_NS;
    uint64_t start = tp_process_monotonic_ns();
    int status = 0, exited = 0, killed = 0;
    while (child > 0 && !exited)
    {
        pid_t waited = waitpid(child, &status, killed ? 0 : WNOHANG);
        if (waited == child) exited = 1;
        else if (waited < 0 && errno != EINTR)
        {
            /* The child is unreachable: stop polling and refuse. */
            exited = 1;
            killed = 1;
        }
        else if (!killed && tp_process_monotonic_ns() - start > limit)
        {
            kill(child, SIGKILL);
            killed = 1;
        }
        else if (!killed)
        {
            struct timespec pause = {0, TP_COMPOSE_ADAPTER_POLL_NS};
            nanosleep(&pause, NULL);
        }
    }
    valid = valid && child > 0 && exited && !killed && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
            fstat(fd, &after) == 0 && after.st_dev == before.st_dev && after.st_ino == before.st_ino &&
            after.st_size == before.st_size && after.st_mtim.tv_sec == before.st_mtim.tv_sec &&
            after.st_mtim.tv_nsec == before.st_mtim.tv_nsec && after.st_ctim.tv_sec == before.st_ctim.tv_sec &&
            after.st_ctim.tv_nsec == before.st_ctim.tv_nsec;
    if (fd >= 0 && close(fd) != 0) valid = 0;
    return valid;
}

/* Run the reviewed `bench_throughput retirement-replay` adapter on the
 * scratch copies of the series manifest and shards, check its output, then
 * seal it unchanged. */
BUSTER_GLOBAL_LOCAL int tp_compose_adapter(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    struct stat info = {0};
    int valid = tp_compose_adapter_run(state);
    int fd = valid ? openat(request->scratch_root, TP_RETIREMENT_COMPOSE_REPLAY_PATH,
                            O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
    valid = valid && fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
            (uint64_t)info.st_size <= state->bounds.replay;
    size_t length = valid ? (size_t)info.st_size : 0, used = 0;
    char* bytes = valid ? (char*)tp_retirement_compose_allocate(state->arena, length) : NULL;
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
    if (request->scratch_root >= 0)
    {
        unlinkat(request->scratch_root, TP_RETIREMENT_COMPOSE_REPLAY_PATH, 0);
        unlinkat(request->scratch_root, TP_RETIREMENT_COMPOSE_SERIES_PATH, 0);
        for (unsigned s = 0; s < state->series_shard_count; ++s)
        {
            char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
            tp_compose_series_shard_path(path, s);
            unlinkat(request->scratch_root, path, 0);
        }
    }
    return valid;
}

/* The post-sample execution receipt in D's canonical encoding
 * (tp_retirement_transcript_receipt), for the composed context. */
BUSTER_GLOBAL_LOCAL int tp_compose_receipt(TpComposeState* state)
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

/* The retained manifest (TP_RETIREMENT_RETAINED_MANIFEST_HEADER, then
 * `kind sha256 bytes path` per retained file in path order): the producer
 * authority binds its digest, so the unsealed A/A evidence is bound too. */
BUSTER_GLOBAL_LOCAL int tp_compose_retained(TpComposeState* state)
{
    TpRetirementComposeDeclaration const* declaration = state->request->declaration;
    TpComposeWriter writer;
    tp_compose_writer_begin(&writer, state->store, TP_RETIREMENT_RETAINED_MANIFEST_PATH, state->bounds.retained, NULL);
    tp_compose_writer_format(&writer, "%s", TP_RETIREMENT_RETAINED_MANIFEST_HEADER);
    unsigned written = 0;
    for (unsigned i = 0; writer.valid && i < state->files; ++i)
    {
        unsigned index = state->by_path[i];
        TpRetirementStoredFile const* file = state->store->files + index;
        if (state->classes[index] == TP_COMPOSE_CLASS_RETAINED)
        {
            tp_compose_writer_format(&writer, "%s %s %" PRIu64 " %s\n", declaration->retained[state->declared[index]].kind,
                                     file->sha256, file->bytes, file->path);
            ++written;
        }
    }
    writer.valid = writer.valid && written == state->retained_files;
    int valid = tp_compose_writer_publish(&writer, &state->retained);
    return valid;
}

BUSTER_GLOBAL_LOCAL void tp_compose_descriptor(TpComposeWriter* writer, TpRetirementComposeArtifact const* artifact)
{
    tp_compose_writer_format(writer, "{\"bytes\":%" PRIu64 ",\"path\":\"%s\",\"sha256\":\"%s\"}", artifact->bytes,
                             artifact->path, artifact->sha256);
}

BUSTER_GLOBAL_LOCAL void tp_compose_manifest_list(TpComposeWriter* writer, TpComposeState const* state,
                                                  unsigned population)
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
BUSTER_GLOBAL_LOCAL void tp_compose_invocations_sha256(TpComposeFamily const* family, char output[65])
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
BUSTER_GLOBAL_LOCAL int tp_compose_bundle(TpComposeState* state)
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

BUSTER_GLOBAL_LOCAL int tp_compose_entry(TpComposeEntry* entries, unsigned* count, unsigned capacity, char const* path,
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

BUSTER_GLOBAL_LOCAL int tp_compose_store_entry_add(TpComposeState* state, TpComposeEntry* entries, unsigned* count,
                                                   unsigned capacity, char const* path, char const* format,
                                                   unsigned index)
{
    unsigned found = tp_compose_find(state, path);
    TpRetirementStoredFile const* file = found != TP_COMPOSE_NONE ? state->store->files + found : NULL;
    int valid = file && tp_compose_entry(entries, count, capacity, file->path, file->bytes, file->sha256, format, index);
    return valid;
}

/* The validator's exact sealed closure (_sealed_closure_files): the prior
 * pre-replay identities plus every composed and streamed artifact, sorted
 * by name, with unique names and paths; the outer record binds its root.
 * (The by-path order predates the composer's own outputs, which are looked
 * up from their artifacts.) */
BUSTER_GLOBAL_LOCAL int tp_compose_seal(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    unsigned capacity = request->prior_count + request->transcript_count + request->metrics_count +
                        request->untimed_metrics_count + request->sample_counts[0] + request->sample_counts[1] +
                        state->partitions + state->series_shard_count + 8;
    TpComposeEntry* entries = (TpComposeEntry*)tp_retirement_compose_allocate(state->arena,
                                  (uint64_t)capacity * sizeof(TpComposeEntry));
    unsigned* order = (unsigned*)tp_retirement_compose_allocate(state->arena, (uint64_t)capacity * 2 * sizeof(unsigned));
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
    for (unsigned i = 0; valid && i < state->series_shard_count; ++i)
        valid = tp_compose_entry(entries, &count, capacity, state->series_shards[i].path, state->series_shards[i].bytes,
                                 state->series_shards[i].sha256, "workflow.adapter_input.shard.%u", i);
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
    /* Unique paths (sorted by path), then the name order the root binds. */
    for (unsigned i = 0; valid && i < count; ++i) order[i] = i;
    if (valid) tp_compose_sort(order, order + capacity, count, entries[0].path, sizeof(TpComposeEntry));
    for (unsigned i = 1; valid && i < count; ++i) valid = strcmp(entries[order[i - 1]].path, entries[order[i]].path) < 0;
    for (unsigned i = 0; valid && i < count; ++i) order[i] = i;
    if (valid) tp_compose_sort(order, order + capacity, count, entries[0].name, sizeof(TpComposeEntry));
    for (unsigned i = 1; valid && i < count; ++i) valid = strcmp(entries[order[i - 1]].name, entries[order[i]].name) < 0;
    /* The files array is hashed alone for the root and embedded verbatim. */
    size_t array_capacity = (size_t)count * TP_COMPOSE_SEAL_ENTRY_BYTES + 16, array_length = 0;
    char* array = valid ? (char*)tp_retirement_compose_allocate(state->arena, array_capacity) : NULL;
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
    return valid;
}

/* ------------------------------------------------------------ entry point */

BUSTER_GLOBAL_LOCAL int tp_compose_request_check(TpRetirementComposeRequest const* request)
{
    TpRetirementPlan const* plan = request ? request->statistics : NULL;
    int valid = request && request->store && request->layout && plan && request->declaration && request->sealed_path &&
                tp_retirement_token(request->job) && tp_retirement_token(request->boot) && request->attempt &&
                request->bound_at_ns && request->completed_at_ns > request->bound_at_ns &&
                tp_compose_digest(request->execution_plan_sha256) && tp_compose_digest(request->source_rows_sha256) &&
                tp_compose_digest(request->result_input_plan_sha256) && tp_compose_digest(request->family_sha256) &&
                tp_compose_digest(request->post_aa_binding_sha256) && tp_compose_digest(request->binding_sha256) &&
                tp_retirement_receipt_path(request->sealed_path) && !tp_compose_reserved_path(request->sealed_path) &&
                plan->version == TP_RETIREMENT_STATISTICS_VERSION && plan->frozen_before_samples == 1 && plan->seed &&
                plan->pairs_per_round >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND &&
                plan->pairs_per_round <= TP_RETIREMENT_EXECUTION_MAX_PAIRS && !(plan->pairs_per_round & 1) &&
                plan->resamples >= TP_RETIREMENT_MIN_RESAMPLES && plan->resamples <= TP_RETIREMENT_MAX_RESAMPLES &&
                request->metrics_count <= TP_RETIREMENT_METRICS_SHARDS &&
                request->untimed_metrics_count <= TP_RETIREMENT_METRICS_SHARDS;
    return valid;
}

/* Working tables sized from the verified family: ratio series, the numeric
 * observation tables, member samples, line buffers and store indexes. */
BUSTER_GLOBAL_LOCAL int tp_compose_workspace(TpComposeState* state)
{
    TpRetirementComposeRequest const* request = state->request;
    TpComposeFamily const* family = &state->family;
    uint64_t units[TP_COMPOSE_OBSERVATIONS] = {family->cells[0], family->cells[0], family->runtime_count,
                                               family->object_count, family->object_count};
    int valid = 1;
    for (unsigned m = 0; valid && m < TP_RETIREMENT_COMPOSE_METRICS; ++m)
    {
        state->ratios[m] = (double*)tp_retirement_compose_allocate(state->arena,
                               (uint64_t)family->cells[m] * state->per_unit * sizeof(double));
        valid = state->ratios[m] != NULL;
    }
    for (unsigned t = 0; valid && t < TP_COMPOSE_OBSERVATIONS; ++t)
    {
        state->observations[t] = units[t] ? (uint64_t*)tp_retirement_compose_allocate(state->arena,
                                     units[t] * state->per_unit * 2 * sizeof(uint64_t)) : NULL;
        valid = !units[t] || state->observations[t];
    }
    uint64_t shard_count = (uint64_t)request->transcript_count + request->sample_counts[0] + request->sample_counts[1];
    unsigned files = state->store->count;
    state->files = files;
    state->shards = valid && shard_count ? (TpComposeShard*)tp_retirement_compose_allocate(state->arena,
                        shard_count * sizeof(TpComposeShard)) : NULL;
    state->member_intervals = valid ? (uint64_t*)tp_retirement_compose_allocate(state->arena,
                                  (uint64_t)TP_RETIREMENT_BATCH_INPUTS * 2 * sizeof(uint64_t)) : NULL;
    state->line = valid ? (char*)tp_retirement_compose_allocate(state->arena, TP_COMPOSE_LINE_BYTES) : NULL;
    state->metrics_line = valid ? (char*)tp_retirement_compose_allocate(state->arena,
                              TP_RETIREMENT_METRICS_LINE_BYTES + 1) : NULL;
    state->by_path = valid && files ? (unsigned*)tp_retirement_compose_allocate(state->arena,
                         (uint64_t)files * 3 * sizeof(unsigned)) : NULL;
    state->classes = valid && files ? (unsigned char*)tp_retirement_compose_allocate(state->arena, files) : NULL;
    valid = valid && state->shards && state->member_intervals && state->line && state->metrics_line && state->by_path &&
            state->classes;
    if (valid)
    {
        state->member_memory = state->member_intervals + TP_RETIREMENT_BATCH_INPUTS;
        state->declared = state->by_path + 2 * (size_t)files;
        state->transcript = state->shards;
        state->samples[0] = state->transcript + request->transcript_count;
        state->samples[1] = state->samples[0] + request->sample_counts[0];
        for (unsigned i = 0; i < files; ++i) state->by_path[i] = i;
        tp_compose_sort(state->by_path, state->by_path + files, files, state->store->files[0].path,
                        sizeof(TpRetirementStoredFile));
        for (unsigned i = 1; valid && i < files; ++i)
            valid = strcmp(state->store->files[state->by_path[i - 1]].path, state->store->files[state->by_path[i]].path) < 0;
    }
    return valid;
}

int tp_retirement_compose(TpRetirementComposeRequest const* request, TpRetirementComposeResult* result)
{
    TpComposeState state = {0};
    char const* stage = "request";
    char digest[65];
    state.request = request;
    int valid = result && tp_compose_request_check(request) && !request->store->failed && request->store->planned &&
                !request->store->active && request->store->count;
    if (result) *result = (TpRetirementComposeResult){0};
    state.store = valid ? request->store : NULL;
    state.pairs = valid ? request->statistics->pairs_per_round : 0;
    state.per_unit = (uint64_t)TP_RETIREMENT_ROUNDS * state.pairs;
    state.arena = valid ? tp_compose_arena() : NULL;
    valid = valid && state.arena;
    if (valid)
    {
        stage = "family";
        valid = tp_compose_family_build(request->layout, &state.family, state.arena);
    }
    TpRetirementComposeShape shape = {request ? request->layout : NULL, state.pairs,
                                      request ? request->code_count : 0, request ? request->prior_count : 0};
    if (valid)
    {
        stage = "bounds";
        valid = tp_compose_bounds_of(&shape, &state.family, &state.bounds) &&
                request->statistics->bootstrap_members_per_scope == state.family.bootstrap &&
                request->statistics->cell_members_per_scope == state.family.cells_total &&
                tp_compose_workspace(&state);
    }
    if (valid)
    {
        stage = "retained";
        valid = request->store->retained_bound && tp_compose_declaration_digest(request->declaration, digest) &&
                !strcmp(digest, request->store->retained_sha256);
    }
    if (valid)
    {
        stage = "inventory";
        valid = tp_compose_inventory(&state);
    }
    char timed_tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1] = {0}, untimed_tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1] = {0};
    if (valid)
    {
        stage = "metrics-shards";
        valid = tp_compose_tiling_init(&state.timed, state.store, request->metrics_paths, request->metrics_count,
                                       state.metrics_line, timed_tag) &&
                tp_compose_tiling_init(&state.untimed_tiling, state.store, request->untimed_metrics_paths,
                                       request->untimed_metrics_count, state.metrics_line, untimed_tag) &&
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
        stage = "aa-transcript";
        valid = tp_compose_aa(&state, timed_tag, untimed_tag);
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
    /* Every input is verified and the series shards are planned: settle the
     * reservation to the exact final inventory (only bounded-kind slack may
     * be released), then publish the composer's outputs. */
    if (valid)
    {
        stage = "series-plan";
        valid = tp_compose_series_plan(&state);
    }
    if (valid)
    {
        stage = "settle";
        valid = tp_retirement_store_settle(state.store, state.store->count + state.bounds.files -
                                                        state.bounds.series_shards + state.series_shard_count);
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
        stage = "retained-manifest";
        valid = tp_compose_retained(&state);
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
        valid = tp_retirement_store_validate(state.store);
    }
    if (valid)
    {
        strcpy(result->raw_measurements_sha256, state.raw_sha256);
        strcpy(result->context_sha256, state.context_sha256);
        result->receipt = state.receipt;
        result->bundle = state.bundle;
        result->sealed = state.sealed;
        result->series = state.series;
        result->series_shards = state.series_shard_count;
        result->replay = state.replay;
        result->code = state.code;
        result->retained = state.retained;
        result->members = state.family.count;
        result->seal_entries = state.seal_entries;
        result->retained_files = state.retained_files;
        result->invocations = state.invocations;
        result->untimed_records = state.untimed_records;
        result->untimed_production = state.untimed_production;
    }
    else
    {
        if (result) result->refused = stage;
        tp_compose_tiling_abandon(&state.timed);
        tp_compose_tiling_abandon(&state.untimed_tiling);
        if (request && request->store) request->store->failed = 1;
    }
    if (state.arena) arena_destroy(state.arena, 1);
    return valid;
}
#endif
