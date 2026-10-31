// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original CLI controls. Each call receives a bounded argv and an isolated temporary destination;
// successful artifacts are independently read, reparsed and observed. Process controls also execute moved replay scripts.

static bool ir_reduce_test_driver_path(char* path, uint64_t capacity, const char* directory, const char* name)
{
    const int length = snprintf(path, capacity, "%s/%s", directory, name);
    const bool valid = length > 0 && (uint64_t)length < capacity;
    return valid;
}

static bool ir_reduce_test_driver_write(const char* path, const void* bytes, uint64_t count)
{
    FILE* file = fopen(path, "wx");
    bool valid = file != NULL && fwrite(bytes, 1, count, file) == count;
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

static bool ir_reduce_test_driver_read(const char* path, Arena* storage, String8* result)
{
    const uint64_t mark = storage->length;
    FILE* file = fopen(path, "rb");
    bool valid = file != NULL && fseek(file, 0, SEEK_END) == 0;
    const long length = valid ? ftell(file) : -1;
    valid = valid && length > 0 && length < (1 << 20) && fseek(file, 0, SEEK_SET) == 0;
    char* bytes = valid ? arena_push(storage, (uint64_t)length + 1, 1) : NULL;
    valid = valid && bytes != NULL && fread(bytes, 1, (uint64_t)length, file) == (uint64_t)length && fgetc(file) == EOF && !ferror(file);
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    if (valid)
    {
        bytes[length] = 0;
        *result = (String8){ .pointer = bytes, .length = (uint64_t)length };
    }
    else
    {
        arena_pop_to(storage, mark);
    }
    return valid;
}

static void ir_reduce_test_driver_remove(const char* directory)
{
    const char* const names[] = { "original.ir", "reduced.ir", "replay.sh", "result.json", "sentinel" };
    char path[PATH_MAX];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        if (ir_reduce_test_driver_path(path, sizeof(path), directory, names[i]))
        {
            unlink(path);
        }
    }
    rmdir(directory);
}

static int ir_reduce_test_driver_run(const char* destination, const char* input, const char* demo, const char* injection,
                                    uint32_t fields, IrReduceFailure expected, const char* control, const char* value, bool scalar)
{
    // A syntactically valid caller assertion is test data; the CLI deliberately does not resolve Git revisions.
    char* arguments[40];
    uint32_t count = 0;
    arguments[count++] = "--revision";
    arguments[count++] = "0123456789abcdef0123456789abcdef01234567";
    arguments[count++] = "--output";
    arguments[count++] = (char*)destination;
    arguments[count++] = "--inject";
    arguments[count++] = (char*)injection;
    arguments[count++] = "--memory";
    // Keep the driver's default budget: interpreter memory registries are part of each private execution view.
    arguments[count++] = "134217728";
    arguments[count++] = "--milliseconds";
    arguments[count++] = "60000";
    arguments[count++] = "--predicate-ms";
    arguments[count++] = "30000";
    arguments[count++] = "--arg";
    arguments[count++] = "0";
    arguments[count++] = "--seed";
    arguments[count++] = "733";
    arguments[count++] = "--max-candidates";
    arguments[count++] = "512";
    if (demo != NULL)
    {
        arguments[count++] = "--demo";
        arguments[count++] = (char*)demo;
    }
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    const char* const names[] = { "--expect-kind", "--expect-pass", "--expect-detail" };
    const uint32_t numbers[] = { expected.kind, expected.pass, expected.detail };
    char encoded[3][24];
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if ((fields & (1u << i)) != 0)
        {
            snprintf(encoded[i], sizeof(encoded[i]), "%u", numbers[i]);
            arguments[count++] = (char*)names[i];
            arguments[count++] = encoded[i];
        }
    }
    if (control != NULL)
    {
        arguments[count++] = (char*)control;
        arguments[count++] = (char*)value;
    }
    if (input != NULL)
    {
        arguments[count++] = (char*)input;
    }
    assert(count < BUSTER_ARRAY_LENGTH(arguments));
    arguments[count] = NULL;
    const int result = run_ir_reduce((int)count, arguments);
    return result;
}

static bool ir_reduce_test_driver_absent(const char* path)
{
    struct stat metadata;
    errno = 0;
    const bool absent = lstat(path, &metadata) != 0 && errno == ENOENT;
    return absent;
}

static bool ir_reduce_test_driver_same(const char* first, const char* second, uint32_t count, Arena* storage)
{
    const uint64_t mark = storage->length;
    const char* const names[] = { "original.ir", "reduced.ir", "replay.sh", "result.json" };
    bool valid = count <= BUSTER_ARRAY_LENGTH(names);
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        char paths[2][PATH_MAX];
        String8 texts[2] = { 0 };
        valid = ir_reduce_test_driver_path(paths[0], sizeof(paths[0]), first, names[i]) &&
                ir_reduce_test_driver_path(paths[1], sizeof(paths[1]), second, names[i]) &&
                ir_reduce_test_driver_read(paths[0], storage, texts) && ir_reduce_test_driver_read(paths[1], storage, texts + 1) &&
                texts[0].length == texts[1].length && memcmp(texts[0].pointer, texts[1].pointer, texts[0].length) == 0;
        arena_pop_to(storage, mark);
    }
    return valid;
}

static bool ir_reduce_test_driver_reparse(const char* directory, IrReduceInjection injection, IrReduceFailure expected, Arena* storage)
{
    const uint64_t mark = storage->length;
    Arena functions = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 23);
    IrFunction parsed[2] = { 0 };
    const char* const names[] = { "original.ir", "reduced.ir", "replay.sh", "result.json" };
    String8 artifacts[4] = { 0 };
    bool valid = functions.base != NULL && scratch.base != NULL;
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        char path[PATH_MAX];
        valid = ir_reduce_test_driver_path(path, sizeof(path), directory, names[i]) &&
                ir_reduce_test_driver_read(path, storage, artifacts + i);
        if (valid && i < 2)
        {
            const IrParseResult result = ir_parse_scalar(artifacts[i], &functions, &scratch, parsed + i);
            valid = result.error == IR_PARSE_OK && result.consumed == artifacts[i].length &&
                    ir_verify_complete(parsed + i, &scratch, true).error == IR_OK &&
                    ir_verify_complete(parsed + i, &scratch, false).error == IR_OK;
        }
    }
    if (valid)
    {
        IrReduceOracle oracle = {
            .argument_count = 1,
            .passes = 7,
            .injection = injection,
            .verifier = injection == IR_REDUCE_INJECT_VERIFY_PHI,
        };
        const IrReduceLimits limits = ir_reduce_limits();
        const IrReduceObservation original = ir_reduce_oracle(parsed, &limits, &oracle, &scratch);
        const IrReduceObservation reduced = ir_reduce_oracle(parsed + 1, &limits, &oracle, &scratch);
        char expected_flags[96];
        char expected_manifest[96];
        snprintf(expected_flags, sizeof(expected_flags), "--expect-kind %u --expect-pass %u --expect-detail %u", expected.kind, expected.pass, expected.detail);
        snprintf(expected_manifest, sizeof(expected_manifest), "\"failure\": {\"kind\": %u, \"pass\": %u, \"detail\": %u}", expected.kind, expected.pass, expected.detail);
        valid = parsed[1].instruction_count < parsed[0].instruction_count &&
                original.status == IR_REDUCE_OBSERVED && reduced.status == IR_REDUCE_OBSERVED &&
                ir_reduce_same_failure(original.failure, expected) && ir_reduce_same_failure(reduced.failure, expected) &&
                strstr(artifacts[2].pointer, "BUSTER_COMPILER") != NULL && strstr(artifacts[2].pointer, "./reduced.ir") != NULL &&
                strstr(artifacts[2].pointer, " --scalar") != NULL &&
                strstr(artifacts[2].pointer, expected_flags) != NULL &&
                strstr(artifacts[3].pointer, "\"format\": \"buster-ir-reduce-v1\"") != NULL &&
                strstr(artifacts[3].pointer, "\"replayed\": true") != NULL && strstr(artifacts[3].pointer, expected_manifest) != NULL;
    }
    arena_release(&scratch);
    arena_release(&functions);
    arena_pop_to(storage, mark);
    return valid;
}

#include "ir_reduce_process_test.c"

static void ir_reduce_test_driver(IrReduceTest* test)
{
    ir_reduce_test_process(test);
    char temporary[] = "/tmp/buster-ir-reduce-driver-XXXXXX";
    char* directory = mkdtemp(temporary);
    Arena storage = arena_reserve(UINT64_C(1) << 20);
    const bool ready = directory != NULL && storage.base != NULL;
    ir_reduce_test_expect(test, ready, "driver controls own a fresh temporary directory and independent artifact storage");
    char bundles[7][PATH_MAX];
    const char* const bundle_names[] = { "phi-vector", "phi-scalar", "memory-vector", "memory-scalar", "regular-file", "phi-repeat", "memory-repeat" };
    bool paths_ready = ready;
    for (uint32_t i = 0; paths_ready && i < BUSTER_ARRAY_LENGTH(bundles); i += 1)
    {
        paths_ready = ir_reduce_test_driver_path(bundles[i], sizeof(bundles[i]), directory, bundle_names[i]);
    }
    for (uint32_t fixture = 0; paths_ready && fixture < 2; fixture += 1)
    {
        const bool memory = fixture != 0;
        const IrReduceFailure expected = {
            .kind = memory ? IR_REDUCE_FAILURE_MEMORY : IR_REDUCE_FAILURE_RESULT,
            .pass = 7,
        };
        const IrReduceInjection injection = memory ? IR_REDUCE_INJECT_MEMORY_STORE : IR_REDUCE_INJECT_RESULT_PHI;
        const char* demo = memory ? "memory" : "phi";
        const char* inject = memory ? "memory-store" : "result-phi";
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const char* bundle = bundles[fixture * 2 + scalar];
            const int status = ir_reduce_test_driver_run(bundle, NULL, demo, inject, 7, expected, NULL, NULL, scalar != 0);
            ir_reduce_test_expect(test, status == 0 && ir_reduce_test_driver_reparse(bundle, injection, expected, &storage),
                                  "phi/memory CLI artifacts independently parse, verify, shrink and reproduce the exact requested class");
        }
        const int repeated = ir_reduce_test_driver_run(bundles[5 + fixture], NULL, demo, inject, 7, expected, NULL, NULL, false);
        ir_reduce_test_expect(test, repeated == 0 && ir_reduce_test_driver_same(bundles[fixture * 2], bundles[5 + fixture], 4, &storage),
                              "repeated same-configuration CLI runs produce identical original/reduced/replay/manifest bytes");
        ir_reduce_test_expect(test, ir_reduce_test_driver_same(bundles[fixture * 2], bundles[fixture * 2 + 1], 2, &storage),
                              "vector/scalar parser modes publish identical canonical IR with independently checked failure classes");
    }
    if (paths_ready)
    {
        ir_reduce_test_process_replay(test, directory, bundles[0], &storage);
        const IrReduceFailure expected = { .kind = IR_REDUCE_FAILURE_RESULT, .pass = 7 };
        char refused[PATH_MAX];
        char input[PATH_MAX];
        char malformed[PATH_MAX];
        char fifo[PATH_MAX];
        char sentinel[PATH_MAX];
        const bool names = ir_reduce_test_driver_path(refused, sizeof(refused), directory, "refused") &&
                           ir_reduce_test_driver_path(input, sizeof(input), directory, "source.ir") &&
                           ir_reduce_test_driver_path(malformed, sizeof(malformed), directory, "malformed.ir") &&
                           ir_reduce_test_driver_path(fifo, sizeof(fifo), directory, "fifo.ir") &&
                           ir_reduce_test_driver_path(sentinel, sizeof(sentinel), bundles[0], "sentinel");
        ir_reduce_test_expect(test, names, "driver control paths fit bounded temporary storage");
        const char* const invalid_budgets[] = { " -1", "\t-1", "+1", " 1", "18446744073709551616" };
        for (uint32_t i = 0; names && i < BUSTER_ARRAY_LENGTH(invalid_budgets); i += 1)
        {
            const int status = ir_reduce_test_driver_run(refused, NULL, "phi", "result-phi", 7, expected,
                                                        "--max-candidates", invalid_budgets[i], false);
            ir_reduce_test_expect(test, status == 1 && ir_reduce_test_driver_absent(refused),
                                  "unsigned budgets reject signs, whitespace and overflow without publishing artifacts");
            ir_reduce_test_driver_remove(refused);
        }
        for (uint32_t fields = 1; names && fields < 7; fields += 1)
        {
            const int status = ir_reduce_test_driver_run(refused, NULL, "phi", "result-phi", fields, expected, NULL, NULL, false);
            ir_reduce_test_expect(test, status == 1 && ir_reduce_test_driver_absent(refused),
                                  "every incomplete expected-kind/pass/detail subset refuses before creating a directory");
            ir_reduce_test_driver_remove(refused);
        }
        for (uint32_t field = 0; names && field < 3; field += 1)
        {
            IrReduceFailure wrong = expected;
            if (field == 0)
            {
                wrong.kind += 1;
            }
            if (field == 1)
            {
                wrong.pass += 1;
            }
            if (field == 2)
            {
                wrong.detail += 1;
            }
            const int status = ir_reduce_test_driver_run(refused, NULL, "phi", "result-phi", 7, wrong, NULL, NULL, false);
            ir_reduce_test_expect(test, status == 1 && ir_reduce_test_driver_absent(refused),
                                  "each different requested failure component refuses without publishing a directory");
            ir_reduce_test_driver_remove(refused);
        }
        if (names)
        {
            const char marker[] = "preserve-existing-destination\0tail";
            const bool marked = ir_reduce_test_driver_write(sentinel, marker, sizeof(marker));
            const int existing = ir_reduce_test_driver_run(bundles[0], NULL, "phi", "result-phi", 7, expected, NULL, NULL, false);
            String8 read_marker = { 0 };
            const bool unchanged = marked && ir_reduce_test_driver_read(sentinel, &storage, &read_marker) && read_marker.length == sizeof(marker) &&
                                   memcmp(read_marker.pointer, marker, sizeof(marker)) == 0 && ir_reduce_test_driver_same(bundles[0], bundles[5], 4, &storage);
            ir_reduce_test_expect(test, existing != 0 && unchanged, "an existing bundle and embedded-NUL sentinel remain byte-identical after destination refusal");
            arena_pop_to(&storage, 0);
            const bool written = ir_reduce_test_driver_write(input, ir_reduce_test_phi, strlen(ir_reduce_test_phi));
            const int regular = written ? ir_reduce_test_driver_run(bundles[4], input, NULL, "result-phi", 0, expected, NULL, NULL, false) : 1;
            ir_reduce_test_expect(test, regular == 0 && ir_reduce_test_driver_reparse(bundles[4], IR_REDUCE_INJECT_RESULT_PHI, expected, &storage),
                                  "a real regular-file input succeeds with the optional expected-class triple absent");
            Arena replay_scratch = arena_reserve(UINT64_C(64) << 20);
            IrReduceOracle replay_oracle = { .argument_count = 1, .passes = 7, .injection = IR_REDUCE_INJECT_RESULT_PHI };
            const IrReduceLimits replay_limits = ir_reduce_limits();
            Arena denied = replay_scratch;
            denied.capacity = 4095;
            const IrReducePublication exhausted = ir_reduce_driver_replay(input, &replay_oracle, &replay_limits, expected, &denied);
            IrReduceFailure different = expected;
            different.detail += 1;
            const IrReducePublication mismatch = replay_scratch.base != NULL ?
                ir_reduce_driver_replay(input, &replay_oracle, &replay_limits, different, &replay_scratch) : IR_REDUCE_PUBLISHED;
            ir_reduce_test_expect(test, exhausted == IR_REDUCE_PUBLICATION_UNAVAILABLE && denied.length == 0 &&
                                  mismatch == IR_REDUCE_PUBLICATION_REFUSED && replay_scratch.length == 0,
                                  "publication replay distinguishes unavailable workspace from a different classified failure");
            arena_release(&replay_scratch);
            const char invalid[] = "function () -> i64 {\nb0:\n%0 = unknown_opcode.i64\n}\n";
            const bool bad_written = ir_reduce_test_driver_write(malformed, invalid, sizeof(invalid) - 1);
            const int bad = bad_written ? ir_reduce_test_driver_run(refused, malformed, NULL, "result-phi", 7, expected, NULL, NULL, false) : 0;
            ir_reduce_test_expect(test, bad_written && bad == 1 && ir_reduce_test_driver_absent(refused), "malformed regular input publishes no directory");
            ir_reduce_test_driver_remove(refused);
            const int limited = ir_reduce_test_driver_run(refused, NULL, "phi", "result-phi", 7, expected, "--steps", "1", false);
            ir_reduce_test_expect(test, limited == 2 && ir_reduce_test_driver_absent(refused), "bounded interpreter exhaustion remains unavailable and publishes no directory");
            ir_reduce_test_driver_remove(refused);
            const int small = ir_reduce_test_driver_run(refused, NULL, "phi", "result-phi", 7, expected, "--memory", "1048575", false);
            ir_reduce_test_expect(test, small == 1 && ir_reduce_test_driver_absent(refused), "an out-of-range memory budget refuses before allocation or publication");
            ir_reduce_test_driver_remove(refused);
            const bool made_fifo = mkfifo(fifo, 0600) == 0;
            const int pipe_input = made_fifo ? ir_reduce_test_driver_run(refused, fifo, NULL, "result-phi", 7, expected, NULL, NULL, false) : 0;
            ir_reduce_test_expect(test, made_fifo && pipe_input != 0 && ir_reduce_test_driver_absent(refused), "FIFO input is rejected through the nonblocking regular-file boundary");
            ir_reduce_test_driver_remove(refused);
            const int directory_input = ir_reduce_test_driver_run(refused, directory, NULL, "result-phi", 7, expected, NULL, NULL, false);
            ir_reduce_test_expect(test, directory_input != 0 && ir_reduce_test_driver_absent(refused), "directory input is rejected without creating an output bundle");
            ir_reduce_test_driver_remove(refused);
            unlink(fifo);
            unlink(malformed);
            unlink(input);
        }
    }
    if (paths_ready)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bundles); i += 1)
        {
            ir_reduce_test_driver_remove(bundles[i]);
        }
    }
    if (directory != NULL)
    {
        ir_reduce_test_expect(test, rmdir(directory) == 0, "driver controls release every temporary artifact and directory");
    }
    arena_release(&storage);
}
