// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Standalone regular-file boundary and replay bundle. A new output directory owns every artifact; the manifest
// is written only after reduced text independently parses, verifies and reproduces the classified failure.
#include <fcntl.h>
#include "ir_reduce_process.c"

typedef enum IrReducePublication
{
    IR_REDUCE_PUBLISHED,
    IR_REDUCE_PUBLICATION_REFUSED,
    IR_REDUCE_PUBLICATION_UNAVAILABLE,
} IrReducePublication;

static bool ir_reduce_driver_number(const char* text, uint64_t* result)
{
    char* end = NULL;
    errno = 0;
    const uint64_t number = strtoull(text, &end, 0);
    const bool valid = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0 && errno == 0;
    if (valid)
    {
        *result = number;
    }
    return valid;
}

static bool ir_reduce_driver_argument(const char* text, InterpreterValue* result)
{
    char* end = NULL;
    errno = 0;
    InterpreterValue value = { .low = strtoull(text, &end, 0) };
    bool valid = text[0] != 0 && end != text && errno == 0;
    if (valid && *end == ':')
    {
        const char* high = end + 1;
        value.high = strtoull(high, &end, 0);
        valid = high[0] != 0 && end != high && errno == 0;
    }
    valid = valid && *end == 0;
    if (valid)
    {
        *result = value;
    }
    return valid;
}

static bool ir_reduce_driver_revision(const char* revision)
{
    const uint64_t length = revision != NULL ? strlen(revision) : 0;
    bool valid = length == 40 || length == 64;
    for (uint64_t i = 0; valid && i < length; i += 1)
    {
        const char c = revision[i];
        valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
    return valid;
}

static bool ir_reduce_driver_read(const char* path, Arena* arena, String8* result)
{
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    struct stat metadata = { 0 };
    bool valid = descriptor >= 0 && fstat(descriptor, &metadata) == 0 && S_ISREG(metadata.st_mode) && metadata.st_size > 0;
    FILE* file = valid ? fdopen(descriptor, "rb") : NULL;
    valid = valid && file != NULL;
    char* bytes = valid ? arena_push(arena, (uint64_t)metadata.st_size, 1) : NULL;
    valid = valid && bytes != NULL && fread(bytes, 1, (uint64_t)metadata.st_size, file) == (uint64_t)metadata.st_size && fgetc(file) == EOF && !ferror(file);
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    else if (descriptor >= 0)
    {
        close(descriptor);
    }
    if (valid)
    {
        *result = (String8){ .pointer = bytes, .length = (uint64_t)metadata.st_size };
    }
    return valid;
}

static IrReducePublication ir_reduce_driver_print(const char* path, const IrFunction* function, Arena* scratch)
{
    FILE* file = fopen(path, "wx");
    const IrError printed = file != NULL ? ir_print(function, scratch, file) : IR_ERROR_MEMORY;
    IrReducePublication result = IR_REDUCE_PUBLICATION_UNAVAILABLE;
    if (file != NULL)
    {
        const bool written = !ferror(file);
        const bool closed = fclose(file) == 0;
        if (written && closed && !ir_reduce_resource(printed))
        {
            result = printed == IR_OK ? IR_REDUCE_PUBLISHED : IR_REDUCE_PUBLICATION_REFUSED;
        }
    }
    return result;
}

static void ir_reduce_driver_quote(FILE* file, const char* text)
{
    fputc('\'', file);
    for (const char* at = text; *at != 0; at += 1)
    {
        if (*at == '\'')
        {
            fputs("'\\''", file);
        }
        else
        {
            fputc(*at, file);
        }
    }
    fputc('\'', file);
}

static bool ir_reduce_driver_binary(uint64_t* hash, uint64_t* bytes, char* path, uint64_t capacity)
{
    const ssize_t length = capacity > 1 ? readlink("/proc/self/exe", path, capacity - 1) : -1;
    const bool complete = length > 0 && (uint64_t)length < capacity - 1;
    FILE* file = complete ? fopen("/proc/self/exe", "rb") : NULL;
    bool valid = file != NULL;
    uint64_t value = UINT64_C(14695981039346656037);
    uint64_t size = 0;
    if (complete)
    {
        path[length] = 0;
    }
    uint8_t block[4096];
    for (uint64_t got = valid ? fread(block, 1, sizeof(block), file) : 0; got != 0; got = fread(block, 1, sizeof(block), file))
    {
        for (uint64_t i = 0; i < got; i += 1)
        {
            value = (value ^ block[i]) * UINT64_C(1099511628211);
        }
        size += got;
    }
    if (file != NULL)
    {
        const bool read = !ferror(file);
        const bool closed = fclose(file) == 0;
        valid = read && closed;
    }
    if (valid)
    {
        *hash = value;
        *bytes = size;
    }
    return valid;
}

static IrReducePublication ir_reduce_driver_replay(const char* path, IrReduceOracle* oracle, const IrReduceLimits* limits,
                                     IrReduceFailure failure, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint64_t bytes = (scratch->capacity - mark) / 4 & ~UINT64_C(63);
    uint8_t* base = arena_push(scratch, bytes * 4, 64);
    IrReducePublication result = IR_REDUCE_PUBLICATION_UNAVAILABLE;
    if (base != NULL && bytes >= 4096)
    {
        Arena text = { .base = base, .capacity = bytes };
        Arena output = { .base = base + bytes, .capacity = bytes };
        Arena checks = { .base = base + bytes * 2, .capacity = bytes * 2 };
        String8 source = { 0 };
        IrFunction function = { 0 };
        const bool read = ir_reduce_driver_read(path, &text, &source);
        const IrParseResult parsed = read ? ir_parse_scalar(source, &output, &checks, &function) : (IrParseResult){ .error = IR_PARSE_MEMORY };
        if (parsed.error != IR_PARSE_MEMORY && parsed.error != IR_PARSE_LIMIT)
        {
            result = IR_REDUCE_PUBLICATION_REFUSED;
            if (parsed.error == IR_PARSE_OK && parsed.consumed == source.length)
            {
                const IrCheck vector = ir_verify_complete(&function, &checks, true);
                const IrCheck scalar = ir_verify_complete(&function, &checks, false);
                if (ir_reduce_resource(vector.error) || ir_reduce_resource(scalar.error))
                {
                    result = IR_REDUCE_PUBLICATION_UNAVAILABLE;
                }
                else if (vector.error == IR_OK && scalar.error == IR_OK)
                {
                    IrReduceProcess process = {
                        .predicate = ir_reduce_oracle,
                        .context = oracle,
                        .started = ir_reduce_milliseconds(),
                    };
                    IrReduceWork replay = {
                        .limits = *limits,
                        .predicate = ir_reduce_process,
                        .context = &process,
                        .scratch = checks,
                        .started = process.started,
                    };
                    const IrReduceObservation observed = ir_reduce_predicate(&function, &replay);
                    result = observed.status != IR_REDUCE_OBSERVED ? IR_REDUCE_PUBLICATION_UNAVAILABLE :
                             ir_reduce_same_failure(observed.failure, failure) ? IR_REDUCE_PUBLISHED : IR_REDUCE_PUBLICATION_REFUSED;
                }
            }
        }
    }
    arena_pop_to(scratch, mark);
    return result;
}

static IrReducePublication ir_reduce_driver_bundle(const char* directory, const char* revision, uint64_t seed, uint64_t memory,
                                     const IrFunction* original, const IrReduceResult* reduced, IrReduceOracle* oracle,
                                     const IrReduceLimits* limits, Arena* scratch)
{
    static const char* const names[] = { "original.ir", "reduced.ir", "replay.sh", "result.json" };
    char paths[4][PATH_MAX];
    char binary[PATH_MAX];
    uint64_t binary_hash = 0;
    uint64_t binary_bytes = 0;
    IrReducePublication result = IR_REDUCE_PUBLICATION_UNAVAILABLE;
    bool valid = ir_reduce_driver_binary(&binary_hash, &binary_bytes, binary, sizeof(binary));
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(paths); i += 1)
    {
        const int length = snprintf(paths[i], sizeof(paths[i]), "%s/%s", directory, names[i]);
        valid = length > 0 && (uint64_t)length < sizeof(paths[i]);
    }
    const bool made = valid && mkdir(directory, 0700) == 0;
    if (valid && !made && errno == EEXIST)
    {
        result = IR_REDUCE_PUBLICATION_REFUSED;
    }
    valid = valid && made;
    if (valid)
    {
        result = ir_reduce_driver_print(paths[0], original, scratch);
        if (result == IR_REDUCE_PUBLISHED)
        {
            result = ir_reduce_driver_print(paths[1], &reduced->function, scratch);
        }
        if (result == IR_REDUCE_PUBLISHED)
        {
            result = ir_reduce_driver_replay(paths[1], oracle, limits, reduced->failure, scratch);
        }
        valid = result == IR_REDUCE_PUBLISHED;
    }
    FILE* replay = valid ? fopen(paths[2], "wx") : NULL;
    valid = valid && replay != NULL;
    if (replay != NULL)
    {
        fputs("#!/bin/sh\nset -eu\ncd -- \"$(dirname -- \"$0\")\"\n: \"${BUSTER_COMPILER:?Set BUSTER_COMPILER to the matching compiler executable}\"\n\"$BUSTER_COMPILER\"", replay);
        fprintf(replay, " reduce-ir --revision %s --output ./replayed", revision);
        fprintf(replay, " --expect-binary-hash 0x%016" PRIx64 " --expect-binary-bytes %" PRIu64, binary_hash, binary_bytes);
        fprintf(replay, " --max-candidates %" PRIu64 " --milliseconds %" PRIu64 " --predicate-ms %" PRIu64
                        " --memory %" PRIu64 " --steps %" PRIu64 " --frames %u --passes %u --seed %" PRIu64,
                limits->max_candidates, limits->max_milliseconds, limits->predicate_milliseconds, memory,
                limits->execution_steps, limits->call_frames, oracle->passes, seed);
        fprintf(replay, " --expect-kind %u --expect-pass %u --expect-detail %u", reduced->failure.kind,
                reduced->failure.pass, reduced->failure.detail);
        // The independent publication check reparses with the scalar parser; replay that validated path.
        fputs(" --scalar", replay);
        if (oracle->verifier)
        {
            fputs(" --verify", replay);
        }
        if (oracle->injection != IR_REDUCE_INJECT_NONE)
        {
            const char* injected = oracle->injection == IR_REDUCE_INJECT_VERIFY_PHI ? "verify-phi" :
                                   oracle->injection == IR_REDUCE_INJECT_RESULT_PHI ? "result-phi" : "memory-store";
            fprintf(replay, " --inject %s", injected);
        }
        for (uint32_t i = 0; i < oracle->argument_count; i += 1)
        {
            fprintf(replay, " --arg 0x%016" PRIx64 ":0x%016" PRIx64, oracle->arguments[i].low, oracle->arguments[i].high);
        }
        fputc(' ', replay);
        ir_reduce_driver_quote(replay, "./reduced.ir");
        fputc('\n', replay);
        const bool written = !ferror(replay);
        const bool closed = fclose(replay) == 0;
        valid = written && closed && valid;
    }
    FILE* manifest = valid ? fopen(paths[3], "wx") : NULL;
    valid = valid && manifest != NULL;
    if (manifest != NULL)
    {
        fprintf(manifest, "{\n  \"format\": \"buster-ir-reduce-v1\",\n  \"source_revision\": \"%s\",\n"
                          "  \"binary_fnv1a64\": \"%016" PRIx64 "\",\n  \"binary_bytes\": %" PRIu64 ",\n"
                          "  \"seed\": %" PRIu64 ",\n  \"status\": %u,\n  \"failure\": {\"kind\": %u, \"pass\": %u, \"detail\": %u},\n"
                          "  \"limits\": {\"candidates\": %" PRIu64 ", \"milliseconds\": %" PRIu64 ", \"predicate_ms\": %" PRIu64
                          ", \"arena_capacity_bytes\": %" PRIu64 ", \"private_workspace_bytes\": %" PRIu64
                          ", \"execution_steps\": %" PRIu64 ", \"call_frames\": %u},\n"
                          "  \"original\": {\"blocks\": %" PRIu64 ", \"instructions\": %" PRIu64 ", \"metadata\": %" PRIu64 "},\n"
                          "  \"reduced\": {\"blocks\": %" PRIu64 ", \"instructions\": %" PRIu64 ", \"metadata\": %" PRIu64 "},\n"
                          "  \"work\": {\"candidates\": %" PRIu64 ", \"predicates\": %" PRIu64 ", \"accepted\": %" PRIu64
                          ", \"cache_hits\": %" PRIu64 ", \"invalid\": %" PRIu64 ", \"different_failure\": %" PRIu64
                          ", \"predicate_faults\": %" PRIu64 ", \"predicate_limits\": %" PRIu64 "},\n"
                          "  \"replayed\": true\n}\n",
                revision, binary_hash, binary_bytes, seed, reduced->status, reduced->failure.kind, reduced->failure.pass, reduced->failure.detail,
                limits->max_candidates, limits->max_milliseconds, limits->predicate_milliseconds, memory, limits->memory_bytes,
                limits->execution_steps, limits->call_frames, reduced->original.blocks, reduced->original.instructions, reduced->original.metadata,
                reduced->reduced.blocks, reduced->reduced.instructions, reduced->reduced.metadata, reduced->candidates, reduced->predicates,
                reduced->accepted, reduced->cache_hits, reduced->invalid, reduced->different, reduced->faults, reduced->limits);
        const bool written = !ferror(manifest);
        const bool closed = fclose(manifest) == 0;
        valid = written && closed;
    }
    if (made && !valid)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(paths); i += 1)
        {
            unlink(paths[i]);
        }
        rmdir(directory);
    }
    if (!valid && result == IR_REDUCE_PUBLISHED)
    {
        result = IR_REDUCE_PUBLICATION_UNAVAILABLE;
    }
    return result;
}

static int run_ir_reduce(int argc, char** argv)
{
    IrReduceLimits limits = ir_reduce_limits();
    IrReduceOracle oracle = { .passes = 7 };
    IrReduceFailure expected = { 0 };
    uint32_t expected_fields = 0;
    uint32_t binary_fields = 0;
    uint64_t expected_binary_hash = 0;
    uint64_t expected_binary_bytes = 0;
    uint64_t memory = UINT64_C(128) << 20;
    uint64_t seed = 0;
    const char* path = NULL;
    const char* demo = NULL;
    const char* directory = NULL;
    const char* revision = NULL;
    bool scalar = false;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const char* option = argv[i];
        if (strcmp(option, "--verify") == 0)
        {
            oracle.verifier = true;
        }
        else if (strcmp(option, "--scalar") == 0)
        {
            scalar = true;
        }
        else if (option[0] == '-' && i + 1 < argc)
        {
            const char* value = argv[++i];
            uint64_t number = 0;
            if (strcmp(option, "--output") == 0)
            {
                directory = value;
            }
            else if (strcmp(option, "--revision") == 0)
            {
                revision = value;
            }
            else if (strcmp(option, "--demo") == 0)
            {
                demo = strcmp(value, "phi") == 0 ? ir_reduce_test_phi : strcmp(value, "memory") == 0 ? ir_reduce_test_memory : NULL;
                valid = demo != NULL;
            }
            else if (strcmp(option, "--inject") == 0)
            {
                oracle.injection = strcmp(value, "verify-phi") == 0 ? IR_REDUCE_INJECT_VERIFY_PHI :
                                   strcmp(value, "result-phi") == 0 ? IR_REDUCE_INJECT_RESULT_PHI :
                                   strcmp(value, "memory-store") == 0 ? IR_REDUCE_INJECT_MEMORY_STORE : IR_REDUCE_INJECT_NONE;
                valid = oracle.injection != IR_REDUCE_INJECT_NONE;
                oracle.verifier |= oracle.injection == IR_REDUCE_INJECT_VERIFY_PHI;
            }
            else if (strcmp(option, "--arg") == 0)
            {
                valid = oracle.argument_count < BUSTER_ARRAY_LENGTH(oracle.arguments) &&
                        ir_reduce_driver_argument(value, oracle.arguments + oracle.argument_count);
                oracle.argument_count += valid;
            }
            else if (ir_reduce_driver_number(value, &number))
            {
                if (strcmp(option, "--expect-kind") == 0)
                {
                    valid = number > 0 && number <= UINT32_MAX;
                    expected.kind = (uint32_t)number;
                    expected_fields |= 1;
                }
                else if (strcmp(option, "--expect-pass") == 0)
                {
                    valid = number <= UINT32_MAX;
                    expected.pass = (uint32_t)number;
                    expected_fields |= 2;
                }
                else if (strcmp(option, "--expect-detail") == 0)
                {
                    valid = number <= UINT32_MAX;
                    expected.detail = (uint32_t)number;
                    expected_fields |= 4;
                }
                else if (strcmp(option, "--expect-binary-hash") == 0)
                {
                    expected_binary_hash = number;
                    binary_fields |= 1;
                }
                else if (strcmp(option, "--expect-binary-bytes") == 0)
                {
                    valid = number != 0;
                    expected_binary_bytes = number;
                    binary_fields |= 2;
                }
                else if (strcmp(option, "--max-candidates") == 0) limits.max_candidates = number;
                else if (strcmp(option, "--milliseconds") == 0) limits.max_milliseconds = number;
                else if (strcmp(option, "--predicate-ms") == 0) limits.predicate_milliseconds = number;
                else if (strcmp(option, "--memory") == 0) memory = number;
                else if (strcmp(option, "--steps") == 0) limits.execution_steps = number;
                else if (strcmp(option, "--frames") == 0) { valid = number > 0 && number <= UINT32_MAX; limits.call_frames = (uint32_t)number; }
                else if (strcmp(option, "--passes") == 0) { valid = number <= 7; oracle.passes = (uint32_t)number; }
                else if (strcmp(option, "--seed") == 0) seed = number;
                else valid = false;
            }
            else
            {
                valid = false;
            }
        }
        else if (path == NULL && option[0] != '-')
        {
            path = option;
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && directory != NULL && ir_reduce_driver_revision(revision) && ((path != NULL) != (demo != NULL)) &&
            (expected_fields == 0 || expected_fields == 7) && (binary_fields == 0 || binary_fields == 3) &&
            memory >= (UINT64_C(1) << 20) && memory <= (UINT64_C(1) << 30) && limits.max_candidates != 0 &&
            limits.max_milliseconds != 0 && limits.predicate_milliseconds != 0 && limits.execution_steps != 0 &&
            (!oracle.verifier || oracle.injection == IR_REDUCE_INJECT_NONE || oracle.injection == IR_REDUCE_INJECT_VERIFY_PHI);
    int result = 1;
    bool identity = true;
    if (valid && binary_fields == 3)
    {
        char executable[PATH_MAX];
        uint64_t hash = 0;
        uint64_t bytes = 0;
        const bool available = ir_reduce_driver_binary(&hash, &bytes, executable, sizeof(executable));
        identity = available && hash == expected_binary_hash && bytes == expected_binary_bytes;
        if (!identity)
        {
            fprintf(stderr, "ir-reduce: replay requires the recorded compiler executable; no bundle published\n");
            result = available ? 1 : 2;
        }
    }
    if (valid && identity)
    {
        const uint64_t eighth = memory / 8 & ~UINT64_C(63);
        Arena source = arena_reserve(eighth);
        Arena input = arena_reserve(eighth);
        Arena output = arena_reserve(eighth);
        Arena scratch = arena_reserve(eighth * 5);
        limits.memory_bytes = eighth * 4;
        String8 text = { .pointer = (char*)demo, .length = demo != NULL ? strlen(demo) : 0 };
        bool ready = source.base != NULL && input.base != NULL && output.base != NULL && scratch.base != NULL &&
                     (demo != NULL || ir_reduce_driver_read(path, &source, &text));
        IrFunction function = { 0 };
        const IrParseResult parsed = ready ? (scalar ? ir_parse_scalar(text, &input, &scratch, &function) :
                                                        ir_parse(text, &input, &scratch, &function)) : (IrParseResult){ .error = IR_PARSE_MEMORY };
        ready = ready && parsed.error == IR_PARSE_OK && parsed.consumed == text.length;
        if (ready && oracle.argument_count == 0 && function.parameter_count <= BUSTER_ARRAY_LENGTH(oracle.arguments))
        {
            oracle.argument_count = function.parameter_count;
        }
        IrReduceResult reduced = { 0 };
        IrReduceProcess process = {
            .predicate = ir_reduce_oracle,
            .context = &oracle,
            .started = ir_reduce_milliseconds(),
        };
        const IrReduceStatus status = ready ? ir_reduce(&function, ir_reduce_process, &process, &limits, &output, &scratch, &reduced) :
                                      parsed.error == IR_PARSE_MEMORY || parsed.error == IR_PARSE_LIMIT ? IR_REDUCE_RESOURCE : IR_REDUCE_INVALID;
        const bool reproduces = ready && status <= IR_REDUCE_CACHE_LIMIT &&
                                (expected_fields == 0 || ir_reduce_same_failure(expected, reduced.failure));
        const IrReducePublication publication = reproduces ?
            ir_reduce_driver_bundle(directory, revision, seed, memory, &function, &reduced, &oracle, &limits, &scratch) :
            IR_REDUCE_PUBLICATION_REFUSED;
        if (reproduces && publication == IR_REDUCE_PUBLISHED)
        {
            printf("ir-reduce: status=%u blocks=%" PRIu64 "->%" PRIu64 " instructions=%" PRIu64 "->%" PRIu64
                   " candidates=%" PRIu64 " predicate=%" PRIu64 " failure=%u/%u/%u\n",
                   status, reduced.original.blocks, reduced.reduced.blocks, reduced.original.instructions, reduced.reduced.instructions,
                   reduced.candidates, reduced.predicates, reduced.failure.kind, reduced.failure.pass, reduced.failure.detail);
            result = 0;
        }
        else
        {
            fprintf(stderr, "ir-reduce: no bundle published (parse=%u, reduction=%u, publication=%u); "
                            "input must reproduce the selected classified failure and publication/replay must be available\n",
                    parsed.error, status, publication);
            result = status == IR_REDUCE_RESOURCE || status == IR_REDUCE_ORACLE_UNAVAILABLE ||
                     publication == IR_REDUCE_PUBLICATION_UNAVAILABLE ? 2 : 1;
        }
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input);
        arena_release(&source);
    }
    else if (!valid)
    {
        fprintf(stderr, "usage: reduce-ir --revision COMMIT --output NEWDIR [--verify] [--passes 0..7] [--arg LOW[:HIGH]] "
                        "[--max-candidates N] [--milliseconds N] [--predicate-ms N] [--memory BYTES] [--steps N] [--frames N] "
                        "[--seed N] [--scalar] [--expect-binary-hash N --expect-binary-bytes N] "
                        "[--inject verify-phi|result-phi|memory-store] FILE.ir\n");
    }
    return result;
}
