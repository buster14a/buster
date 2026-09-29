/* Strict reader for the compiler's own per-input metrics text (#881, A1).
 * One object batch writes `-fmetrics-out`: a `CC_METRICS` header, then per
 * input one `CC_METRICS_INPUT` line followed by its `CC_METRICS_FUNCTION`
 * lines. Every line is `TAG key=value ...` with the keys in a pinned order;
 * numbers are canonical decimals and strings lowercase hex (`-` when empty).
 * This reader never trusts the compiler: an unknown, missing or reordered key,
 * a second schema version, a count that disagrees with the lines, a status or
 * diagnostic digest that differs from the frozen oracle, or an interval outside
 * the process rejects the whole batch. It mirrors the validator's
 * `_check_batch_metrics`; neither side skips a key it does not know.
 * The key order pins version 1 as published with #1823 (docs/agents/driver.md
 * at 35b6b64): start/end offsets from the origin shared with `wall_ns`, the
 * diagnostic record count and digest, the `intervals` header field, and texts
 * cut at 1,024 bytes with their full length and a `*_truncated` flag.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_METRICS_H
#define BUSTER_THROUGHPUT_RETIREMENT_METRICS_H
#include <buster/lib/hash.h>
#include <stdint.h>
#include <string.h>

#define TP_RETIREMENT_METRICS_VERSION 1u
/* The #615 record bound for one line and the per-artifact byte cap. */
#define TP_RETIREMENT_METRICS_LINE_BYTES UINT64_C(1048576)
#define TP_RETIREMENT_METRICS_ARTIFACT_BYTES UINT64_C(67108864)
/* One batch holds every fixture of one recipe group plus its controls. */
#define TP_RETIREMENT_BATCH_INPUTS 1024u
/* Messages and function names are cut here, with their full length recorded. */
#define TP_RETIREMENT_METRICS_TEXT_LIMIT 1024u

/* One member's per-input sample and the census row it belongs to. */
typedef struct TpRetirementMemberSample
{
    uint64_t interval_ns, peak_memory_bytes;
    unsigned row;
} TpRetirementMemberSample;

/* A control's `row` when it names no canonical row. */
#define TP_RETIREMENT_BATCH_NO_ROW 0xffffffffu

/* One frozen batch input: timed members first (ascending census row), then
 * controls. `artifact` is the object leaf in the private output directory.
 * A control's `row` is TP_RETIREMENT_BATCH_NO_ROW or a canonical row outside
 * the timed projection. */
typedef struct TpRetirementBatchInput
{
    char const* fixture;
    char const* status;
    char const* error;
    char const* diagnostic_sha256;
    char const* object_sha256;
    char const* artifact;
    unsigned member, row;
} TpRetirementBatchInput;

typedef struct TpRetirementBatchContract
{
    char const* target;
    char const* allocator;
    char const* metrics;
    TpRetirementBatchInput const* inputs;
    unsigned input_count, exit_status;
} TpRetirementBatchContract;

typedef struct TpRetirementMetricsValue
{
    uint64_t number;
    char const* text;
    unsigned length;
} TpRetirementMetricsValue;

static char const* const tp_retirement_metrics_header_fields[] = {
    "version", "schema", "inputs", "records", "ok", "rejected", "failed", "not_run",
    "prebuilt", "error", "exit_status", "action", "target", "allocator", "compile_jobs",
    "compilation_workers", "intervals", "keep_going", "function_sizes", "wall_ns",
    "peak_rss_bytes"};
enum
{
    TP_METRICS_H_VERSION, TP_METRICS_H_SCHEMA, TP_METRICS_H_INPUTS, TP_METRICS_H_RECORDS,
    TP_METRICS_H_OK, TP_METRICS_H_REJECTED, TP_METRICS_H_FAILED, TP_METRICS_H_NOT_RUN,
    TP_METRICS_H_PREBUILT, TP_METRICS_H_ERROR, TP_METRICS_H_EXIT_STATUS, TP_METRICS_H_ACTION,
    TP_METRICS_H_TARGET, TP_METRICS_H_ALLOCATOR, TP_METRICS_H_COMPILE_JOBS,
    TP_METRICS_H_WORKERS, TP_METRICS_H_INTERVALS, TP_METRICS_H_KEEP_GOING,
    TP_METRICS_H_FUNCTION_SIZES, TP_METRICS_H_WALL_NS, TP_METRICS_H_PEAK_RSS,
    TP_METRICS_H_COUNT
};

static char const* const tp_retirement_metrics_input_fields[] = {
    "version", "index", "status", "error", "errors", "warnings", "measured", "start_ns",
    "end_ns", "total_ns", "read_ns", "preprocess_ns", "parse_ns", "analysis_ns", "ir_ns",
    "codegen_ns", "object_ns", "emit_ns", "arena_peak_bytes", "arena_retained_bytes",
    "source_bytes", "preprocessed_tokens", "object_file_bytes", "text_bytes", "rodata_bytes",
    "data_bytes", "bss_bytes", "tdata_bytes", "tbss_bytes", "initializer_bytes",
    "unwind_bytes", "debug_bytes", "codegen_functions", "instructions", "values",
    "code_bytes", "stack_frame_bytes", "max_stack_frame_bytes", "fallback_functions",
    "fallback_records", "function_records", "function_records_omitted",
    "diagnostic_records", "diagnostic_digest", "diagnostic_line", "diagnostic_column",
    "path_hex", "diagnostic_code_hex", "diagnostic_path_hex", "message_bytes",
    "message_truncated", "message_hex"};
enum
{
    TP_METRICS_I_VERSION, TP_METRICS_I_INDEX, TP_METRICS_I_STATUS, TP_METRICS_I_ERROR,
    TP_METRICS_I_MEASURED = 6, TP_METRICS_I_START, TP_METRICS_I_END, TP_METRICS_I_TOTAL,
    TP_METRICS_I_FIRST_PHASE, TP_METRICS_I_LAST_PHASE = 17, TP_METRICS_I_ARENA_PEAK,
    TP_METRICS_I_OBJECT_BYTES = 22, TP_METRICS_I_FUNCTION_RECORDS = 40,
    TP_METRICS_I_DIAGNOSTIC_DIGEST = 43, TP_METRICS_I_PATH = 46, TP_METRICS_I_MESSAGE_BYTES = 49,
    TP_METRICS_I_COUNT = 52
};

static char const* const tp_retirement_metrics_function_fields[] = {
    "version", "input", "ordinal", "code_bytes", "name_bytes", "name_truncated", "name_hex"};
enum
{
    TP_METRICS_F_VERSION, TP_METRICS_F_INPUT, TP_METRICS_F_ORDINAL, TP_METRICS_F_NAME_BYTES = 4,
    TP_METRICS_F_COUNT = 7
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(tp_retirement_metrics_header_fields) == TP_METRICS_H_COUNT);
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(tp_retirement_metrics_input_fields) == TP_METRICS_I_COUNT);
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(tp_retirement_metrics_function_fields) == TP_METRICS_F_COUNT);

static inline int tp_retirement_metrics_hex_digest(char const* text, size_t length)
{
    int ok = text && length == 64;
    for (size_t i = 0; ok && i < length; ++i)
        ok = (text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f');
    return ok;
}

/* Value kinds follow the key: words, the one digest, `*_hex`, else numbers. */
static inline unsigned tp_retirement_metrics_kind(char const* field)
{
    static char const* const words[] = {"schema", "error", "action", "target", "allocator",
                                        "intervals", "status"};
    size_t length = strlen(field);
    unsigned kind = 0;
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(words); ++i)
        if (!strcmp(field, words[i])) kind = 1;
    if (!strcmp(field, "diagnostic_digest")) kind = 2;
    if (length > 4 && !strcmp(field + length - 4, "_hex")) kind = 3;
    return kind;
}

static inline int tp_retirement_metrics_word(char const* text, size_t length)
{
    int ok = text && length >= 1 && length <= 128 &&
        ((text[0] >= 'a' && text[0] <= 'z') || (text[0] >= '0' && text[0] <= '9'));
    for (size_t i = 1; ok && i < length; ++i)
        ok = (text[i] >= 'a' && text[i] <= 'z') || (text[i] >= '0' && text[i] <= '9') ||
             text[i] == '_' || text[i] == '.' || text[i] == '-';
    return ok;
}

static inline int tp_retirement_metrics_number(char const* text, size_t length, uint64_t* value)
{
    int ok = text && value && length >= 1 && length <= 20 && (text[0] != '0' || length == 1);
    uint64_t number = 0;
    for (size_t i = 0; ok && i < length; ++i)
    {
        unsigned digit = (unsigned)(text[i] - '0');
        ok = text[i] >= '0' && text[i] <= '9' && number <= (UINT64_MAX - digit) / 10;
        if (ok) number = number * 10 + digit;
    }
    if (value) *value = ok ? number : 0;
    return ok;
}

static inline int tp_retirement_metrics_hex(char const* text, size_t length)
{
    int ok = text && length >= 1 && (length == 1 ? text[0] == '-' : !(length & 1));
    for (size_t i = 0; ok && length > 1 && i < length; ++i)
        ok = (text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f');
    return ok;
}

/* Parse one tagged record (without its LF) with exactly `fields` in order. */
static inline int tp_retirement_metrics_line(char const* line, size_t length, char const* tag,
    char const* const* fields, unsigned count, TpRetirementMetricsValue* values)
{
    size_t tag_length = strlen(tag), position = tag_length;
    int ok = line && values && length > tag_length && !memcmp(line, tag, tag_length);
    for (size_t i = 0; ok && i < length; ++i)
        ok = (unsigned char)line[i] >= 32 && (unsigned char)line[i] <= 126;
    for (unsigned field = 0; ok && field < count; ++field)
    {
        size_t key = strlen(fields[field]);
        ok = position < length && line[position] == ' ' && length - position - 1 > key &&
            !memcmp(line + position + 1, fields[field], key) && line[position + 1 + key] == '=';
        size_t start = position + 2 + key, end = start;
        while (ok && end < length && line[end] != ' ') ++end;
        char const* text = line + start;
        size_t size = end - start;
        unsigned kind = ok ? tp_retirement_metrics_kind(fields[field]) : 0;
        if (ok)
        {
            values[field] = (TpRetirementMetricsValue){.text = text, .length = (unsigned)size};
            ok = size >= 1 && (kind == 1 ? tp_retirement_metrics_word(text, size) :
                kind == 2 ? tp_retirement_metrics_hex_digest(text, size) :
                kind == 3 ? tp_retirement_metrics_hex(text, size) :
                tp_retirement_metrics_number(text, size, &values[field].number));
        }
        position = end;
    }
    ok = ok && position == length && values[0].number == TP_RETIREMENT_METRICS_VERSION;
    return ok;
}

/* `<text>_bytes`, `<text>_truncated`, `<text>_hex`: the recorded text is the
 * first min(bytes, limit) bytes and the flag says whether it was cut. */
static inline int tp_retirement_metrics_truncation(TpRetirementMetricsValue const* values, unsigned bytes_field)
{
    uint64_t length = values[bytes_field].number, truncated = values[bytes_field + 1].number;
    uint64_t recorded = values[bytes_field + 2].length == 1 ? 0 : values[bytes_field + 2].length / 2;
    uint64_t kept = length < TP_RETIREMENT_METRICS_TEXT_LIMIT ? length : TP_RETIREMENT_METRICS_TEXT_LIMIT;
    int ok = truncated <= 1 && truncated == (length > TP_RETIREMENT_METRICS_TEXT_LIMIT) && recorded == kept;
    return ok;
}

static inline int tp_retirement_metrics_text(TpRetirementMetricsValue const* value, char const* expected)
{
    size_t length = expected ? strlen(expected) : 0;
    int equal = value && expected && value->length == length && !memcmp(value->text, expected, length);
    return equal;
}

static inline unsigned tp_retirement_metrics_nibble(char c)
{
    return c >= 'a' ? (unsigned)(c - 'a' + 10) : (unsigned)(c - '0');
}

/* Printable ASCII bytes (0x20..0x7e) with no `..` component. */
static inline int tp_retirement_metrics_clean_path(char const* path, size_t length)
{
    int ok = path && length;
    size_t start = 0;
    for (size_t i = 0; ok && i <= length; ++i)
    {
        if (i == length || path[i] == '/')
        {
            ok = !(i - start == 2 && path[start] == '.' && path[start + 1] == '.');
            start = i + 1;
        }
        else ok = (unsigned char)path[i] >= 0x20 && (unsigned char)path[i] <= 0x7e;
    }
    return ok;
}

/* The recorded input path is exactly the frozen fixture path given on the
 * batch argv: the same rule as the validator, with no prefix or suffix match. */
static inline int tp_retirement_metrics_input_path(TpRetirementMetricsValue const* value, char const* fixture)
{
    size_t fixture_length = fixture ? strlen(fixture) : 0;
    size_t length = value && value->length > 1 ? value->length / 2 : 0;
    int ok = fixture_length && length == fixture_length &&
        tp_retirement_metrics_clean_path(fixture, fixture_length);
    for (size_t i = 0; ok && i < length; ++i)
        ok = tp_retirement_metrics_nibble(value->text[i * 2]) * 16 +
            tp_retirement_metrics_nibble(value->text[i * 2 + 1]) == (unsigned char)fixture[i];
    return ok;
}

static inline int tp_retirement_metrics_status(char const* status)
{
    int ok = status && (!strcmp(status, "ok") || !strcmp(status, "rejected") || !strcmp(status, "failed"));
    return ok;
}

static inline int tp_retirement_metrics_error(char const* error)
{
    size_t length = error ? strlen(error) : 0;
    int ok = length > 7 && length <= 70 && !memcmp(error, "driver.", 7);
    for (size_t i = 7; ok && i < length; ++i)
        ok = (error[i] >= 'a' && error[i] <= 'z') || (error[i] >= '0' && error[i] <= '9') || error[i] == '-';
    return ok;
}

static inline int tp_retirement_metrics_leaf(char const* name)
{
    int ok = name && name[0] && strcmp(name, ".") && strcmp(name, "..");
    for (unsigned i = 0; ok && name[i]; ++i)
        ok = i < 127 && ((name[i] >= 'a' && name[i] <= 'z') ||
            (name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= '0' && name[i] <= '9') ||
            name[i] == '_' || name[i] == '-' || name[i] == '.');
    return ok;
}

static inline int tp_retirement_metrics_fixture(char const* fixture)
{
    size_t length = fixture ? strlen(fixture) : 0;
    int ok = length && length <= 512 && fixture[0] != '/' &&
        tp_retirement_metrics_clean_path(fixture, length);
    return ok;
}

/* The frozen contract is structurally checked before any launch: members
 * first and compiled, controls after them, status/error/object agreement,
 * distinct output leaves, and an exit status that is nonzero exactly when a
 * control fails. The service authenticates these facts; this is consistency. */
static inline int tp_retirement_batch_contract_valid(TpRetirementBatchContract const* contract)
{
    unsigned count = contract ? contract->input_count : 0, failures = 0, members = 0;
    int ok = contract && count && count <= TP_RETIREMENT_BATCH_INPUTS && contract->inputs &&
        tp_retirement_metrics_word(contract->target, contract->target ? strlen(contract->target) : 0) &&
        tp_retirement_metrics_word(contract->allocator, contract->allocator ? strlen(contract->allocator) : 0) &&
        tp_retirement_metrics_leaf(contract->metrics) && contract->exit_status <= 255;
    for (unsigned i = 0; ok && i < count; ++i)
    {
        TpRetirementBatchInput const* input = &contract->inputs[i];
        int compiled = tp_retirement_metrics_status(input->status) && !strcmp(input->status, "ok");
        ok = input->member <= 1 && (input->member ? i == members : 1) &&
            tp_retirement_metrics_fixture(input->fixture) &&
            tp_retirement_metrics_status(input->status) && tp_retirement_metrics_error(input->error) &&
            tp_retirement_metrics_hex_digest(input->diagnostic_sha256,
                input->diagnostic_sha256 ? strlen(input->diagnostic_sha256) : 0) &&
            compiled == !strcmp(input->error, "driver.none") &&
            compiled == (input->object_sha256 != NULL) && compiled == (input->artifact != NULL) &&
            (!input->member || compiled) &&
            (!input->object_sha256 || tp_retirement_metrics_hex_digest(input->object_sha256,
                strlen(input->object_sha256))) &&
            (!input->artifact || (tp_retirement_metrics_leaf(input->artifact) &&
                strcmp(input->artifact, contract->metrics))) &&
            (!input->member || (input->row != TP_RETIREMENT_BATCH_NO_ROW &&
                (!i || input->row > contract->inputs[i - 1].row)));
        for (unsigned previous = 0; ok && input->artifact && previous < i; ++previous)
            ok = !contract->inputs[previous].artifact || strcmp(contract->inputs[previous].artifact, input->artifact);
        /* A control never repeats a member's or another control's row. */
        for (unsigned previous = 0; ok && !input->member && input->row != TP_RETIREMENT_BATCH_NO_ROW &&
             previous < i; ++previous)
            ok = contract->inputs[previous].row != input->row;
        if (ok && input->member) ++members;
        if (ok && !compiled) ++failures;
    }
    ok = ok && members && (contract->exit_status != 0) == (failures != 0);
    return ok;
}

/* Find the next LF-terminated line of at most the #615 record bound. */
static inline int tp_retirement_metrics_next(unsigned char const* bytes, uint64_t size, uint64_t* offset,
    char const** line, size_t* length)
{
    uint64_t start = *offset, end = start;
    uint64_t limit = size - start < TP_RETIREMENT_METRICS_LINE_BYTES ? size - start : TP_RETIREMENT_METRICS_LINE_BYTES;
    while (end - start < limit && bytes[end] != '\n') ++end;
    int ok = start < size && end < size && bytes[end] == '\n' && end - start + 1 <= TP_RETIREMENT_METRICS_LINE_BYTES;
    if (ok)
    {
        *line = (char const*)bytes + start;
        *length = (size_t)(end - start);
        *offset = end + 1;
    }
    return ok;
}

/* Authenticate one batch's metrics bytes against its frozen contract.
 * elapsed_ns is the supervisor's process interval (0 when untimed). Each
 * member's (per-input interval, arena high-water bytes) is returned in input
 * order; controls are status-checked, never samples. */
static inline int tp_retirement_metrics_check(unsigned char const* bytes, uint64_t size,
    TpRetirementBatchContract const* contract, uint64_t elapsed_ns,
    TpRetirementMemberSample* members, unsigned member_capacity)
{
    TpRetirementMetricsValue header[TP_METRICS_H_COUNT], input[TP_METRICS_I_COUNT];
    TpRetirementMetricsValue function[TP_METRICS_F_COUNT];
    uint64_t offset = 0, statuses[5] = {0}, previous_end = 0;
    char const* line = NULL;
    size_t length = 0;
    unsigned member_count = 0;
    char const* first_error = NULL;
    unsigned first_error_length = 0;
    int ok = bytes && size && size <= TP_RETIREMENT_METRICS_ARTIFACT_BYTES &&
        tp_retirement_batch_contract_valid(contract) && members &&
        tp_retirement_metrics_next(bytes, size, &offset, &line, &length) &&
        tp_retirement_metrics_line(line, length, "CC_METRICS", tp_retirement_metrics_header_fields,
            TP_METRICS_H_COUNT, header);
    for (unsigned i = 0; ok && i < contract->input_count; ++i)
    {
        TpRetirementBatchInput const* expected = &contract->inputs[i];
        ok = tp_retirement_metrics_next(bytes, size, &offset, &line, &length) &&
            tp_retirement_metrics_line(line, length, "CC_METRICS_INPUT", tp_retirement_metrics_input_fields,
                TP_METRICS_I_COUNT, input) &&
            input[TP_METRICS_I_INDEX].number == i &&
            tp_retirement_metrics_truncation(input, TP_METRICS_I_MESSAGE_BYTES) &&
            (!input[TP_METRICS_I_FUNCTION_RECORDS].number || header[TP_METRICS_H_FUNCTION_SIZES].number) &&
            tp_retirement_metrics_input_path(&input[TP_METRICS_I_PATH], expected->fixture);
        static char const* const known[] = {"ok", "rejected", "failed", "not_run", "prebuilt"};
        unsigned status = 5;
        for (unsigned k = 0; ok && k < 5; ++k)
            if (tp_retirement_metrics_text(&input[TP_METRICS_I_STATUS], known[k])) status = k;
        ok = ok && status < 5;
        if (ok)
        {
            ++statuses[status];
            if (status && !first_error)
            {
                first_error = input[TP_METRICS_I_ERROR].text;
                first_error_length = input[TP_METRICS_I_ERROR].length;
            }
        }
        /* The frozen oracle: status, error, diagnostics and object presence. */
        ok = ok && tp_retirement_metrics_text(&input[TP_METRICS_I_STATUS], expected->status) &&
            tp_retirement_metrics_text(&input[TP_METRICS_I_ERROR], expected->error) &&
            tp_retirement_metrics_text(&input[TP_METRICS_I_DIAGNOSTIC_DIGEST], expected->diagnostic_sha256) &&
            input[TP_METRICS_I_MEASURED].number == 1 &&
            (input[TP_METRICS_I_OBJECT_BYTES].number > 0) == (expected->object_sha256 != NULL);
        uint64_t start = ok ? input[TP_METRICS_I_START].number : 0;
        uint64_t end = ok ? input[TP_METRICS_I_END].number : 0;
        ok = ok && start >= previous_end && end > start && end <= header[TP_METRICS_H_WALL_NS].number &&
            input[TP_METRICS_I_TOTAL].number == end - start;
        uint64_t phases = 0;
        for (unsigned field = TP_METRICS_I_FIRST_PHASE; ok && field <= TP_METRICS_I_LAST_PHASE; ++field)
        {
            ok = input[field].number <= end - start - phases;
            if (ok) phases += input[field].number;
        }
        previous_end = end;
        if (ok && expected->member)
        {
            ok = member_count < member_capacity && input[TP_METRICS_I_ARENA_PEAK].number > 0;
            if (ok) members[member_count++] = (TpRetirementMemberSample){end - start,
                input[TP_METRICS_I_ARENA_PEAK].number, expected->row};
        }
        uint64_t functions = ok ? input[TP_METRICS_I_FUNCTION_RECORDS].number : 0;
        for (uint64_t ordinal = 0; ok && ordinal < functions; ++ordinal)
            ok = tp_retirement_metrics_next(bytes, size, &offset, &line, &length) &&
                tp_retirement_metrics_line(line, length, "CC_METRICS_FUNCTION",
                    tp_retirement_metrics_function_fields, TP_METRICS_F_COUNT, function) &&
                function[TP_METRICS_F_INPUT].number == i && function[TP_METRICS_F_ORDINAL].number == ordinal &&
                tp_retirement_metrics_truncation(function, TP_METRICS_F_NAME_BYTES);
    }
    ok = ok && offset == size;
    if (ok)
    {
        TpRetirementMetricsValue const none = {.text = "driver.none", .length = 11};
        TpRetirementMetricsValue expected_error = first_error ?
            (TpRetirementMetricsValue){.text = first_error, .length = first_error_length} : none;
        ok = tp_retirement_metrics_text(&header[TP_METRICS_H_SCHEMA], "buster-cc-metrics") &&
            header[TP_METRICS_H_INPUTS].number == contract->input_count &&
            header[TP_METRICS_H_RECORDS].number == contract->input_count &&
            header[TP_METRICS_H_OK].number == statuses[0] &&
            header[TP_METRICS_H_REJECTED].number == statuses[1] &&
            header[TP_METRICS_H_FAILED].number == statuses[2] &&
            header[TP_METRICS_H_NOT_RUN].number == statuses[3] && !statuses[3] &&
            header[TP_METRICS_H_PREBUILT].number == statuses[4] && !statuses[4] &&
            header[TP_METRICS_H_ERROR].length == expected_error.length &&
            !memcmp(header[TP_METRICS_H_ERROR].text, expected_error.text, expected_error.length) &&
            header[TP_METRICS_H_EXIT_STATUS].number == contract->exit_status &&
            tp_retirement_metrics_text(&header[TP_METRICS_H_ACTION], "object") &&
            tp_retirement_metrics_text(&header[TP_METRICS_H_TARGET], contract->target) &&
            tp_retirement_metrics_text(&header[TP_METRICS_H_ALLOCATOR], contract->allocator) &&
            header[TP_METRICS_H_COMPILE_JOBS].number == 1 && header[TP_METRICS_H_WORKERS].number == 1 &&
            tp_retirement_metrics_text(&header[TP_METRICS_H_INTERVALS], "serial") &&
            header[TP_METRICS_H_KEEP_GOING].number == 1 && header[TP_METRICS_H_FUNCTION_SIZES].number <= 1 &&
            header[TP_METRICS_H_WALL_NS].number &&
            (!elapsed_ns || header[TP_METRICS_H_WALL_NS].number <= elapsed_ns);
    }
    if (!ok && members)
        for (unsigned i = 0; i < member_capacity; ++i) members[i] = (TpRetirementMemberSample){0};
    return ok;
}

/* A batch's output digest: SHA-256 of the canonical JSON list of its ordered
 * per-input object digests, with null where an input writes no object. */
static inline int tp_retirement_batch_output_digest(char const* const* objects, unsigned count, char digest[65])
{
    int ok = objects && count && count <= TP_RETIREMENT_BATCH_INPUTS && digest;
    for (unsigned i = 0; ok && i < count; ++i)
        ok = !objects[i] || tp_retirement_metrics_hex_digest(objects[i], strlen(objects[i]));
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, "[", 1);
    for (unsigned i = 0; ok && i < count; ++i)
    {
        if (i) sha256_add(&hash, ",", 1);
        if (!objects[i]) sha256_add(&hash, "null", 4);
        else
        {
            sha256_add(&hash, "\"", 1);
            sha256_add(&hash, objects[i], 64);
            sha256_add(&hash, "\"", 1);
        }
    }
    sha256_add(&hash, "]", 1);
    if (digest) digest[0] = 0;
    if (ok) sha256_finish_hex(&hash, digest);
    return ok;
}

static inline int tp_retirement_batch_contract_output(TpRetirementBatchContract const* contract, char digest[65])
{
    char const* objects[TP_RETIREMENT_BATCH_INPUTS];
    int ok = tp_retirement_batch_contract_valid(contract);
    for (unsigned i = 0; ok && i < contract->input_count; ++i) objects[i] = contract->inputs[i].object_sha256;
    ok = ok && tp_retirement_batch_output_digest(objects, contract->input_count, digest);
    if (!ok && digest) digest[0] = 0;
    return ok;
}

static inline void tp_retirement_batch_contract_text(Sha256* hash, char const* text)
{
    uint64_t length = text ? strlen(text) : UINT64_MAX;
    unsigned char bytes[8];
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (unsigned char)(length >> (i * 8));
    sha256_add(hash, bytes, sizeof(bytes));
    if (text) sha256_add(hash, text, length);
}

/* Identity of every frozen contract field, for freeze/run comparison. */
static inline int tp_retirement_batch_contract_digest(TpRetirementBatchContract const* contract, char digest[65])
{
    int ok = tp_retirement_batch_contract_valid(contract);
    if (digest) digest[0] = 0;
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        static char const domain[] = "tp-retirement-batch-contract-v1";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        tp_retirement_batch_contract_text(&hash, contract->target);
        tp_retirement_batch_contract_text(&hash, contract->allocator);
        tp_retirement_batch_contract_text(&hash, contract->metrics);
        unsigned char numbers[8] = {(unsigned char)contract->input_count,
            (unsigned char)(contract->input_count >> 8), (unsigned char)(contract->input_count >> 16),
            (unsigned char)(contract->input_count >> 24), (unsigned char)contract->exit_status, 0, 0, 0};
        sha256_add(&hash, numbers, sizeof(numbers));
        for (unsigned i = 0; i < contract->input_count; ++i)
        {
            TpRetirementBatchInput const* input = &contract->inputs[i];
            unsigned char member[5] = {(unsigned char)input->member, (unsigned char)input->row,
                (unsigned char)(input->row >> 8), (unsigned char)(input->row >> 16),
                (unsigned char)(input->row >> 24)};
            sha256_add(&hash, member, sizeof(member));
            tp_retirement_batch_contract_text(&hash, input->fixture);
            tp_retirement_batch_contract_text(&hash, input->status);
            tp_retirement_batch_contract_text(&hash, input->error);
            tp_retirement_batch_contract_text(&hash, input->diagnostic_sha256);
            tp_retirement_batch_contract_text(&hash, input->object_sha256);
            tp_retirement_batch_contract_text(&hash, input->artifact);
        }
        sha256_finish_hex(&hash, digest);
    }
    return ok;
}
#endif
