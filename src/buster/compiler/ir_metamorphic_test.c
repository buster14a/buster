// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original negative/lifetime controls for the metamorphic runner. These validate the test machinery before a
// campaign; intentional invalidity, consumer budgets and semantic mismatches remain distinct classified outcomes.

static bool ir_meta_expect(IrMetaTally* tally, bool valid, const char* message)
{
    tally->checks += 1;
    if (!valid)
    {
        fprintf(stderr, "ir-metamorphic contract: %s\n", message);
    }
    return valid;
}

static bool ir_meta_bad_pair(const IrFunction* function, IrError expected, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const IrCheck vector = ir_verify_complete(function, scratch, true);
    const IrCheck scalar = ir_verify_complete(function, scratch, false);
    const bool valid = vector.error == expected && scalar.error == expected && vector.instruction == scalar.instruction &&
                       vector.detail == scalar.detail && vector.dominance_checked == scalar.dominance_checked && scratch->length == mark;
    return valid;
}

static bool ir_meta_artifact_contracts(IrMetaTally* tally, const IrFunction* function, Arena* scratch, bool* resource)
{
    char root[] = "/tmp/buster-ir-metamorphic-XXXXXX";
    char* created = mkdtemp(root);
    bool valid = ir_meta_expect(tally, created != NULL, "artifact control has a private temporary directory");
    *resource |= created == NULL;
    char directory[PATH_MAX] = { 0 };
    const IrMetaOptions options = { .seed = 731, .count = 1, .max_steps = 100000, .max_ms = 30000,
                                   .artifacts = root, .revision = "contract-source", .silent = true, .inject = IR_META_SEMANTICS };
    const IrMetaFailureRecord record = { .test = ir_meta_case(731, 0), .failure = IR_META_SEMANTICS, .original = function,
                                        .variant_input = function, .transformed = function, .input = 7, .expected = 19 };
    const bool captured = created != NULL && ir_meta_artifact_directory(directory, sizeof(directory), root, &record) &&
                          ir_meta_artifact(&options, &record, scratch);
    valid = ir_meta_expect(tally, captured, "fresh artifacts retain original, variant, consumer output and run metadata") && valid;
    if (captured)
    {
        const bool exact = ir_meta_replay_one(directory, "original.ir", function, (String8){ 0 }, scratch) &&
                           ir_meta_replay_one(directory, "variant.ir", function, (String8){ 0 }, scratch) &&
                           ir_meta_replay_one(directory, "transformed.ir", function, (String8){ 0 }, scratch);
        valid = ir_meta_expect(tally, exact, "replay compares complete canonical input/output bytes") && valid;
        IrMetaOptions loaded = { 0 };
        const bool parsed = ir_meta_load_replay(directory, &loaded);
        valid = ir_meta_expect(tally, parsed && loaded.seed == 731 && loaded.first == 0 && loaded.count == 1 &&
                              loaded.max_steps == options.max_steps && loaded.max_ms == options.max_ms && loaded.replay_input == record.input &&
                              loaded.inject == options.inject && loaded.replay_failure == IR_META_SEMANTICS,
                              "replay recovers version, seed, budgets, injected control and failure location") && valid;
        valid = ir_meta_expect(tally, !ir_meta_artifact(&options, &record, scratch) &&
                              ir_meta_replay_one(directory, "original.ir", function, (String8){ 0 }, scratch),
                              "existing artifact directories are refused without replacing their retained files") && valid;
        char path[PATH_MAX];
        int length = snprintf(path, sizeof(path), "%s/transformed.ir", directory);
        FILE* file = length > 0 && (uint64_t)length < sizeof(path) ? fopen(path, "a") : NULL;
        bool written = file != NULL && fputc('\n', file) != EOF;
        if (file != NULL)
        {
            written = fclose(file) == 0 && written;
        }
        *resource |= !written;
        valid = ir_meta_expect(tally, written && !ir_meta_replay_one(directory, "transformed.ir", function, (String8){ 0 }, scratch),
                              "even a trailing-byte change invalidates a retained consumer output") && valid;
        length = snprintf(path, sizeof(path), "%s/run.txt", directory);
        file = length > 0 && (uint64_t)length < sizeof(path) ? fopen(path, "w") : NULL;
        written = file != NULL && fputs("ir-metamorphic 1\nseed 18446744073709551616\n", file) >= 0;
        if (file != NULL)
        {
            written = fclose(file) == 0 && written;
        }
        *resource |= !written;
        const IrMetaOptions unchanged = loaded;
        valid = ir_meta_expect(tally, written && !ir_meta_load_replay(directory, &loaded) && memcmp(&loaded, &unchanged, sizeof(loaded)) == 0,
                              "numeric overflow or incomplete replay metadata cannot partially publish options") && valid;
    }
    if (created != NULL)
    {
        static const char* const names[] = { "original.ir", "variant.ir", "transformed.ir", "run.txt" };
        for (uint32_t i = 0; directory[0] != 0 && i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            char path[PATH_MAX];
            const int length = snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
            const bool removed = length > 0 && (uint64_t)length < sizeof(path) && (unlink(path) == 0 || errno == ENOENT);
            *resource |= !removed;
        }
        *resource |= directory[0] != 0 && rmdir(directory) != 0 && errno != ENOENT;
        *resource |= rmdir(root) != 0;
    }
    return valid;
}

static IrMetaFailure ir_meta_contracts(IrMetaTally* tally, Arena* source, Arena* output, Arena* scratch)
{
    const uint64_t source_mark = source->length;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    bool valid = true;
    bool resource = false;
    uint64_t number = 37;
    valid = ir_meta_expect(tally, ir_meta_number("18446744073709551615", &number) && number == UINT64_MAX,
                          "numeric options retain the full unsigned seed range") && valid;
    static const char* const bad_numbers[] = { "", "-1", "+1", " 1", "18446744073709551616", "12tail" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bad_numbers); i += 1)
    {
        number = 37;
        valid = ir_meta_expect(tally, !ir_meta_number(bad_numbers[i], &number) && number == 37,
                              "invalid numeric options preserve their unpublished result") && valid;
    }
    // Remapping's exact logical tails are independent of generated CFGs. Inactive lanes contain invalid indices,
    // so any unmasked gather or store becomes visible even when the usual generated instruction count is small.
    for (uint32_t count = 0; count <= 33; count += 1)
    {
        uint32_t map[33];
        uint32_t vector[35];
        uint32_t scalar[35];
        uint8_t masks[33];
        for (uint32_t i = 0; i < 33; i += 1)
        {
            map[i] = 32 - i;
            masks[i] = i % 3 == 0 ? 1 : 0;
        }
        for (uint32_t i = 0; i < 35; i += 1)
        {
            vector[i] = i < count && masks[i] != 0 ? i : UINT32_MAX;
            scalar[i] = vector[i];
        }
        ir_meta_remap_column(vector, masks, 1, count, map, false);
        ir_meta_remap_column(scalar, masks, 1, count, map, true);
        bool exact = memcmp(vector, scalar, sizeof(vector)) == 0;
        for (uint32_t i = 0; exact && i < 35; i += 1)
        {
            exact = vector[i] == (i < count && masks[i] != 0 ? 32 - i : UINT32_MAX);
        }
        valid = ir_meta_expect(tally, exact, "scalar/vector masked ID remapping preserves logical tails and inactive indices") && valid;
    }
    const long page = sysconf(_SC_PAGESIZE);
    void* mapping = page > 0 ? mmap(NULL, (uint64_t)page * 7, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool guarded = mapping != MAP_FAILED;
    for (uint32_t p = 0; guarded && p < 7; p += 2)
    {
        guarded = mprotect((uint8_t*)mapping + (uint64_t)p * (uint64_t)page, (uint64_t)page, PROT_NONE) == 0;
    }
    resource = !guarded;
    valid = ir_meta_expect(tally, guarded, "exact-tail ID-remap guard pages are available") && valid;
    for (uint32_t count = 0; guarded && count <= 33; count += 1)
    {
        uint32_t* map = (uint32_t*)((uint8_t*)mapping + page);
        uint32_t* column = (uint32_t*)((uint8_t*)mapping + (uint64_t)page * 4 - (uint64_t)count * 4);
        uint8_t* masks = (uint8_t*)mapping + (uint64_t)page * 6 - count;
        uint32_t expected[33];
        for (uint32_t i = 0; i < 33; i += 1)
        {
            map[i] = 32 - i;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            masks[i] = i % 3 == 0 ? 1 : 0;
            column[i] = masks[i] != 0 ? i : UINT32_MAX;
            expected[i] = masks[i] != 0 ? 32 - i : UINT32_MAX;
        }
        ir_meta_remap_column(column, masks, 1, count, map, false);
        const bool exact = count == 0 || memcmp(column, expected, (uint64_t)count * 4) == 0;
        valid = ir_meta_expect(tally, exact, "masked loads/gathers/stores never cross inaccessible logical tails") && valid;
    }
    if (mapping != MAP_FAILED)
    {
        resource |= munmap(mapping, (uint64_t)page * 7) != 0;
    }
    char text[BUSTER_IR_META_TEXT];
    const IrMetaCase test = ir_meta_case(731, 0);
    String8 generated = { 0 };
    IrFunction function = { 0 };
    bool ready = ir_meta_generate(&test, text, sizeof(text), &generated) && ir_parse(generated, source, scratch, &function).error == IR_PARSE_OK;
    valid = ir_meta_expect(tally, ready, "independent arithmetic contract source is valid IR") && valid;
    if (ready)
    {
        bool changed = false;
        IrFunction bad = { 0 };
        ready = ir_meta_transform(&function, 0, false, output, scratch, &bad, &changed);
        valid = ir_meta_expect(tally, ready && !changed, "identity rebuilding preserves a verified function") && valid;
        if (ready)
        {
            const uint32_t add = 5;
            const uint32_t operand = bad.a[add];
            bad.a[add] = bad.instruction_count;
            valid = ir_meta_expect(tally, ir_meta_bad_pair(&bad, IR_ERROR_OPERAND, scratch), "out-of-range value IDs are rejected identically, never executed") && valid;
            tally->negative += 1;
            bad.a[add] = operand;
            const uint8_t type = bad.type[add];
            bad.type[add] = IR_VOID;
            valid = ir_meta_expect(tally, ir_meta_bad_pair(&bad, IR_ERROR_TYPE, scratch), "invalid result types retain their exact verifier category") && valid;
            tally->negative += 1;
            bad.type[add] = type;
            const uint32_t last = bad.instruction_count - 1;
            bad.op[last] = IR_COPY;
            bad.type[last] = IR_I64;
            valid = ir_meta_expect(tally, ir_meta_bad_pair(&bad, IR_ERROR_TERMINATOR, scratch), "removing a terminator is rejected before interpretation") && valid;
            tally->negative += 1;
            bad.op[last] = IR_RET;
            bad.type[last] = IR_VOID;
            const IrMetaObservation baseline = ir_meta_execute(&bad, 7, 100000, scratch);
            const uint64_t expected = ir_meta_model(&test, 7);
            valid = ir_meta_expect(tally, ir_meta_observation(&baseline, expected) == IR_META_OK, "independent arithmetic and memory model agrees with execution") && valid;
            valid = ir_meta_expect(tally, ir_meta_observation(&baseline, expected ^ 1) == IR_META_SEMANTICS, "a changed observable value cannot be accepted as a representation change") && valid;
            const IrMetaObservation limited = ir_meta_execute(&bad, 7, 1, scratch);
            valid = ir_meta_expect(tally, ir_meta_observation(&limited, expected) == IR_META_STEPS, "step exhaustion is a budget result, not a semantic mismatch or success") && valid;
            Arena tiny = *output;
            tiny.capacity = tiny.length + 1;
            const uint64_t tiny_mark = tiny.length;
            IrFunction unpublished;
            memset(&unpublished, 0x5a, sizeof(unpublished));
            const IrFunction untouched = unpublished;
            const bool built = ir_meta_transform(&function, 3, true, &tiny, scratch, &unpublished, &changed);
            valid = ir_meta_expect(tally, !built && tiny.length == tiny_mark && memcmp(&unpublished, &untouched, sizeof(unpublished)) == 0 &&
                                  scratch->length == scratch_mark, "failed rebuilding leaves arenas and the unpublished view unchanged") && valid;
            valid = ir_meta_artifact_contracts(tally, &function, scratch, &resource) && valid;
        }
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(source, source_mark);
    const IrMetaCase diamond = ir_meta_case(731, 1);
    ready = ir_meta_generate(&diamond, text, sizeof(text), &generated) && ir_parse(generated, source, scratch, &function).error == IR_PARSE_OK;
    valid = ir_meta_expect(tally, ready, "independent CFG/phi contract source is valid IR") && valid;
    if (ready)
    {
        const uint32_t phi = 12;
        const uint32_t at = function.a[phi];
        const uint32_t predecessor = function.extra[at];
        function.extra[at] = function.block_count;
        valid = ir_meta_expect(tally, ir_meta_bad_pair(&function, IR_ERROR_PHI, scratch), "invalid phi predecessors retain the exact rejection category") && valid;
        function.extra[at] = predecessor;
        tally->negative += 1;
    }
    const char undefined_source[] = "function (i64) -> i64 {\nb0:\n%0 = param.i64 0\n%1 = undef.i64\n%2 = add.i64 %0, %1\n%3 = ret.void %2\n}\n";
    const String8 undefined_text = { .pointer = (char*)undefined_source, .length = sizeof(undefined_source) - 1 };
    IrFunction undefined = { 0 };
    const bool undefined_ready = ir_parse(undefined_text, output, scratch, &undefined).error == IR_PARSE_OK &&
                                 ir_meta_validate(&undefined, scratch) == IR_META_OK;
    valid = ir_meta_expect(tally, undefined_ready, "undefined-value control remains structurally valid IR") && valid;
    if (undefined_ready)
    {
        const IrMetaObservation observation = ir_meta_execute(&undefined, 7, 100000, scratch);
        valid = ir_meta_expect(tally, ir_meta_observation(&observation, 0) == IR_META_UNDEFINED,
                              "consuming an undefined value is distinguished from unsupported execution and semantic mismatch") && valid;
    }
    const char malformed[] = "function (i64) -> i64 {\nb0:\n%0 = unknown_opcode.i64\n}\n";
    const String8 invalid_text = { .pointer = (char*)malformed, .length = sizeof(malformed) - 1 };
    IrFunction invalid[2];
    memset(invalid, 0x5a, sizeof(invalid));
    IrFunction saved[2];
    memcpy(saved, invalid, sizeof(invalid));
    const uint64_t before_source = source->length;
    const uint64_t before_output = output->length;
    const IrParseResult vector = ir_parse(invalid_text, source, scratch, invalid);
    const IrParseResult scalar = ir_parse_scalar(invalid_text, output, scratch, invalid + 1);
    valid = ir_meta_expect(tally, vector.error == IR_PARSE_SYNTAX && scalar.error == IR_PARSE_SYNTAX && vector.offset == scalar.offset &&
                          vector.line == scalar.line && vector.column == scalar.column && vector.consumed == scalar.consumed &&
                          source->length == before_source && output->length == before_output && memcmp(invalid, saved, sizeof(invalid)) == 0,
                          "malformed grammar has stable diagnostics and cannot publish executable IR") && valid;
    tally->negative += 1;
    arena_pop_to(output, output_mark);
    arena_pop_to(source, source_mark);
    valid = ir_meta_expect(tally, scratch->length == scratch_mark, "all negative and success consumers restore scratch") && valid;
    const IrMetaFailure failure = resource ? IR_META_RESOURCE : valid ? IR_META_OK : IR_META_VERIFY;
    return failure;
}
