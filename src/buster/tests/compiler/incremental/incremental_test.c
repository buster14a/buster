// The opt-in code-generation cache (incremental.h): its artifact and pack
// codecs against hostile bytes, and the driver against its own clean output.
// Every end-to-end check compiles the same freestanding unit clean and through
// the cache and requires byte-identical objects, on one target per object
// format and architecture so the result does not depend on the host.
#include <buster/tests/compiler/incremental/incremental_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/incremental/incremental_internal.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

// A decoded artifact equals the one that was encoded, field for field.
BUSTER_GLOBAL_LOCAL bool incremental_test_artifacts_equal(IncrementalFunctionArtifact const* a, IncrementalFunctionArtifact const* b)
{
    bool result = a->code.length == b->code.length && memory_compare(a->code.pointer, b->code.pointer, a->code.length) &&
                  a->prolog_size == b->prolog_size && a->symbol_slot_count == b->symbol_slot_count &&
                  a->unwind_action_count == b->unwind_action_count && a->epilog_count == b->epilog_count && a->block_count == b->block_count &&
                  a->relocation_count == b->relocation_count && a->line_mark_count == b->line_mark_count &&
                  a->debug_location_count == b->debug_location_count;
    for (u32 index = 0; result && index < a->unwind_action_count; index += 1)
    {
        result = a->unwind_actions[index].code_offset == b->unwind_actions[index].code_offset &&
                 a->unwind_actions[index].value == b->unwind_actions[index].value && a->unwind_actions[index].kind == b->unwind_actions[index].kind &&
                 a->unwind_actions[index].register_index == b->unwind_actions[index].register_index;
    }
    for (u32 index = 0; result && index < a->epilog_count; index += 1)
    {
        result = a->epilog_offsets[index] == b->epilog_offsets[index];
    }
    for (u32 index = 0; result && index < a->block_count; index += 1)
    {
        result = a->block_offsets[index] == b->block_offsets[index];
    }
    for (u32 index = 0; result && index < a->relocation_count; index += 1)
    {
        result = a->relocations[index].offset == b->relocations[index].offset && a->relocations[index].symbol_slot == b->relocations[index].symbol_slot &&
                 a->relocations[index].addend == b->relocations[index].addend && a->relocations[index].kind == b->relocations[index].kind;
    }
    for (u32 index = 0; result && index < a->line_mark_count; index += 1)
    {
        result = a->line_marks[index].code_offset == b->line_marks[index].code_offset &&
                 a->line_marks[index].instruction == b->line_marks[index].instruction;
    }
    for (u32 index = 0; result && index < a->debug_location_count; index += 1)
    {
        IncrementalDebugLocation const* left = a->debug_locations + index;
        IncrementalDebugLocation const* right = b->debug_locations + index;
        result = left->local.value == right->local.value && left->start == right->start && left->end == right->end &&
                 left->location.kind == right->location.kind && left->location.reg == right->location.reg &&
                 left->location.frame_offset == right->location.frame_offset && left->location.constant == right->location.constant &&
                 left->location.piece_count == right->location.piece_count;
        for (u32 piece = 0; result && piece < left->location.piece_count; piece += 1)
        {
            DebugLocationPiece const* p = left->location.pieces + piece;
            DebugLocationPiece const* q = right->location.pieces + piece;
            result = p->kind == q->kind && p->reg == q->reg && p->frame_offset == q->frame_offset && p->constant == q->constant &&
                     p->value_offset == q->value_offset && p->size == q->size;
        }
    }
    for (u32 index = 0; result && index < INCREMENTAL_STATISTIC_COUNT; index += 1)
    {
        result = a->statistics[index] == b->statistics[index];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult incremental_test_artifact_codec(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u8 code[64];
    for (u32 index = 0; index < sizeof(code); index += 1)
    {
        code[index] = (u8)(index * 37 + 11);
    }
    CodegenUnwindAction unwind[] = {
        {.code_offset = 1, .value = 0, .kind = CODEGEN_UNWIND_ACTION_PUSH_REGISTER, .register_index = 5},
        {.code_offset = 4, .value = 48, .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK},
    };
    u32 epilogs[] = {40, 60};
    u32 blocks[] = {0, 17, 33};
    IncrementalRelocation relocations[] = {
        {.addend = -4, .offset = 9, .symbol_slot = 1, .kind = CODEGEN_MODULE_RELOCATION_X86_64_PC32},
        {.addend = INT64_MIN, .offset = 20, .symbol_slot = 0, .kind = CODEGEN_MODULE_RELOCATION_ABSOLUTE64},
    };
    // Marks move backward as well as forward: the deltas are signed.
    IncrementalLineMark marks[] = {{.code_offset = 0, .instruction = 3}, {.code_offset = 12, .instruction = 1}, {.code_offset = 12, .instruction = 7}};
    DebugLocationPiece pieces[] = {
        {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RAX, .size = 8},
        {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -24, .value_offset = 8, .size = 8},
    };
    IncrementalDebugLocation locations[] = {
        {.location = {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -16}, .local = {.value = 2}, .start = 4, .end = 50},
        {.location = {.kind = DEBUG_LOCATION_PIECEWISE, .pieces = pieces, .piece_count = 2}, .local = {.value = 0}, .start = 10, .end = 64},
    };
    IncrementalFunctionArtifact artifact = {
        .code = {.pointer = code, .length = sizeof(code)},
        .unwind_actions = unwind,
        .epilog_offsets = epilogs,
        .block_offsets = blocks,
        .relocations = relocations,
        .line_marks = marks,
        .debug_locations = locations,
        .unwind_action_count = BUSTER_ARRAY_LENGTH(unwind),
        .epilog_count = BUSTER_ARRAY_LENGTH(epilogs),
        .block_count = BUSTER_ARRAY_LENGTH(blocks),
        .relocation_count = BUSTER_ARRAY_LENGTH(relocations),
        .line_mark_count = BUSTER_ARRAY_LENGTH(marks),
        .debug_location_count = BUSTER_ARRAY_LENGTH(locations),
        .prolog_size = 4,
        .symbol_slot_count = 2,
    };
    for (u32 index = 0; index < INCREMENTAL_STATISTIC_COUNT; index += 1)
    {
        artifact.statistics[index] = (u64)index * 1000003u;
    }
    IrFunction function = {.block_count = 3, .instruction_count = 8, .local_count = 3};
    ByteSlice encoded = incremental_artifact_encode(arena, &artifact);
    IncrementalFunctionArtifact decoded;
    if (BUSTER_REQUIRE(arguments, encoded.pointer && encoded.length))
    {
        BUSTER_TEST(arguments, incremental_artifact_decode(arena, encoded, &function, 2, &decoded));
        BUSTER_TEST(arguments, incremental_test_artifacts_equal(&artifact, &decoded));
        // The artifact must match the function it is replayed into: another
        // block count or slot table refuses it.
        IrFunction other = function;
        other.block_count = 4;
        BUSTER_TEST(arguments, !incremental_artifact_decode(arena, encoded, &other, 2, &decoded));
        BUSTER_TEST(arguments, !incremental_artifact_decode(arena, encoded, &function, 3, &decoded));
        other = function;
        other.instruction_count = 7;
        BUSTER_TEST(arguments, !incremental_artifact_decode(arena, encoded, &other, 2, &decoded));
        // Every proper prefix is refused, and every single-byte corruption is
        // either refused or decodes within the function's bounds: the decoder
        // never trusts a count or index it has not checked.
        u32 truncations_refused = 0;
        for (u64 length = 0; length < encoded.length; length += 1)
        {
            truncations_refused += !incremental_artifact_decode(arena, (ByteSlice){.pointer = encoded.pointer, .length = length}, &function, 2, &decoded);
        }
        BUSTER_TEST(arguments, truncations_refused == encoded.length);
        u8* copy = arena_allocate(arena, u8, encoded.length);
        u32 bounded = 0;
        u32 trials = 0;
        for (u64 position = 0; position < encoded.length; position += 1)
        {
            for (u32 bit = 0; bit < 8; bit += 1)
            {
                memcpy(copy, encoded.pointer, encoded.length);
                copy[position] ^= (u8)(1u << bit);
                bool accepted = incremental_artifact_decode(arena, (ByteSlice){.pointer = copy, .length = encoded.length}, &function, 2, &decoded);
                bool in_bounds = !accepted || (decoded.block_count == function.block_count && decoded.prolog_size <= decoded.code.length);
                for (u32 index = 0; accepted && index < decoded.relocation_count; index += 1)
                {
                    in_bounds = in_bounds && decoded.relocations[index].symbol_slot < 2 && decoded.relocations[index].offset + 4 <= decoded.code.length;
                }
                for (u32 index = 0; accepted && index < decoded.line_mark_count; index += 1)
                {
                    in_bounds = in_bounds && decoded.line_marks[index].instruction < function.instruction_count &&
                                decoded.line_marks[index].code_offset <= decoded.code.length;
                }
                bounded += in_bounds;
                trials += 1;
            }
        }
        BUSTER_TEST(arguments, bounded == trials);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice incremental_test_pack(Arena* arena, ByteSlice context, u32 count)
{
    IncrementalPackEntry* entries = arena_allocate(arena, IncrementalPackEntry, count);
    IncrementalManifestEntry* manifest = arena_allocate(arena, IncrementalManifestEntry, count + 1);
    for (u32 index = 0; index < count; index += 1)
    {
        String8 record = string_format(arena, S8("record-{u32}-{u32}"), index, index * 7);
        String8 artifact = string_format(arena, S8("artifact-{u32}"), index);
        entries[index] = (IncrementalPackEntry){
            .fingerprint = buster_hash_64((u8*)record.pointer, record.length),
            .record = BUSTER_SLICE_TO_BYTE_SLICE(record),
            .artifact = BUSTER_SLICE_TO_BYTE_SLICE(artifact),
        };
        manifest[index] = (IncrementalManifestEntry){
            .name = string_format(arena, S8("function_{u32}"), count - index), .fingerprint = entries[index].fingerprint, .entry_index = index,
            .sections = {index, index + 1, index + 2},
        };
    }
    // A function whose artifact was not stored keeps its manifest row.
    manifest[count] = (IncrementalManifestEntry){.name = S8("uncaptured"), .entry_index = UINT32_MAX};
    return incremental_pack_encode(arena, context, entries, count, manifest, count + 1);
}

BUSTER_GLOBAL_LOCAL void incremental_test_refingerprint(u8* bytes, u64 length)
{
    u64 body = buster_hash_64(bytes + 64, length - 64);
    memcpy(bytes + 24, &body, 8);
    u64 header = buster_hash_64(bytes, 48);
    memcpy(bytes + 48, &header, 8);
}

BUSTER_GLOBAL_LOCAL UnitTestResult incremental_test_pack_codec(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    ByteSlice context = BUSTER_SLICE_TO_BYTE_SLICE(S8("context record"));
    ByteSlice pack = incremental_test_pack(arena, context, 9);
    // Encoding is a pure function of its inputs.
    ByteSlice again = incremental_test_pack(arena, context, 9);
    BUSTER_TEST(arguments, pack.length == again.length && memory_compare(pack.pointer, again.pointer, pack.length));
    IncrementalPack decoded;
    if (BUSTER_REQUIRE(arguments, incremental_pack_decode(arena, pack, &decoded) == INCREMENTAL_PACK_LOADED))
    {
        BUSTER_TEST(arguments, decoded.entry_count == 9 && decoded.manifest_count == 10);
        BUSTER_TEST(arguments, decoded.context.length == context.length && memory_compare(decoded.context.pointer, context.pointer, context.length));
        String8 record = string_format(arena, S8("record-{u32}-{u32}"), 4u, 28u);
        u64 fingerprint = buster_hash_64((u8*)record.pointer, record.length);
        u32 entry = incremental_pack_find_entry(&decoded, fingerprint, BUSTER_SLICE_TO_BYTE_SLICE(record));
        BUSTER_TEST(arguments, entry < decoded.entry_count && string_equal(BYTE_SLICE_TO_STRING(8, decoded.entries[entry].artifact), S8("artifact-4")));
        // An equal fingerprint with different bytes is not a hit.
        String8 impostor = string_format(arena, S8("record-{u32}-{u32}"), 4u, 29u);
        BUSTER_TEST(arguments, incremental_pack_find_entry(&decoded, fingerprint, BUSTER_SLICE_TO_BYTE_SLICE(impostor)) == UINT32_MAX);
    }
    u8* copy = arena_allocate(arena, u8, pack.length);
    // Any single-byte change or truncation is caught by the fingerprints.
    u32 refused = 0;
    for (u64 position = 0; position < pack.length; position += 1)
    {
        memcpy(copy, pack.pointer, pack.length);
        copy[position] ^= 0x40;
        refused += incremental_pack_decode(arena, (ByteSlice){.pointer = copy, .length = pack.length}, &decoded) != INCREMENTAL_PACK_LOADED;
    }
    BUSTER_TEST(arguments, refused == pack.length);
    refused = 0;
    for (u64 length = 0; length < pack.length; length += 1)
    {
        refused += incremental_pack_decode(arena, (ByteSlice){.pointer = pack.pointer, .length = length}, &decoded) == INCREMENTAL_PACK_CORRUPT;
    }
    BUSTER_TEST(arguments, refused == pack.length);
    // Structure is checked beyond the fingerprints: a rewritten entry order
    // or an out-of-range offset with valid fingerprints is still refused.
    u64 entry_table = 64 + context.length;
    memcpy(copy, pack.pointer, pack.length);
    u8 row[32];
    memcpy(row, copy + entry_table, 32);
    memcpy(copy + entry_table, copy + entry_table + 32, 32);
    memcpy(copy + entry_table + 32, row, 32);
    incremental_test_refingerprint(copy, pack.length);
    BUSTER_TEST(arguments, incremental_pack_decode(arena, (ByteSlice){.pointer = copy, .length = pack.length}, &decoded) == INCREMENTAL_PACK_CORRUPT);
    memcpy(copy, pack.pointer, pack.length);
    u64 wild = pack.length;
    memcpy(copy + entry_table + 8, &wild, 8);
    incremental_test_refingerprint(copy, pack.length);
    BUSTER_TEST(arguments, incremental_pack_decode(arena, (ByteSlice){.pointer = copy, .length = pack.length}, &decoded) == INCREMENTAL_PACK_CORRUPT);
    // A well-formed pack of another format version is a context change.
    memcpy(copy, pack.pointer, pack.length);
    u32 version = INCREMENTAL_FORMAT_VERSION + 1;
    memcpy(copy + 8, &version, 4);
    incremental_test_refingerprint(copy, pack.length);
    BUSTER_TEST(arguments, incremental_pack_decode(arena, (ByteSlice){.pointer = copy, .length = pack.length}, &decoded) ==
                               INCREMENTAL_PACK_CONTEXT_CHANGED);
    return result;
}

// ---------------------------------------------------------------------------
// End to end through the driver.

static String8 const incremental_test_source_v1 = S8_INITIALIZER(
    "struct pair { int a; int b; };\n"
    "static const int table[4] = {1, 2, 3, 4};\n"
    "static int leaf(int x) { return x * 3 + 1; }\n"
    "int sum(int n) { int total = 0; for (int i = 0; i < n; i += 1) { total += table[i & 3]; } return total; }\n"
    "int pair_sum(struct pair const* p) { return p->a + p->b; }\n"
    "int calls(int v) { return leaf(v) + sum(v); }\n"
    "const char* text(void) { return \"incremental\"; }\n"
    "int dispatch(int op) { static void* labels[] = {&&one, &&two}; goto *labels[op & 1]; one: return 1; two: return 2; }\n");

// The same unit with one body changed and every later line moved down.
static String8 const incremental_test_source_v2 = S8_INITIALIZER(
    "struct pair { int a; int b; };\n"
    "static const int table[4] = {1, 2, 3, 4};\n"
    "static int leaf(int x) { return x * 5 + 1; }\n"
    "\n"
    "int sum(int n) { int total = 0; for (int i = 0; i < n; i += 1) { total += table[i & 3]; } return total; }\n"
    "int pair_sum(struct pair const* p) { return p->a + p->b; }\n"
    "int calls(int v) { return leaf(v) + sum(v); }\n"
    "const char* text(void) { return \"incremental\"; }\n"
    "int dispatch(int op) { static void* labels[] = {&&one, &&two}; goto *labels[op & 1]; one: return 1; two: return 2; }\n");

#define INCREMENTAL_TEST_FUNCTIONS 6u

BUSTER_GLOBAL_LOCAL CompilerDriverResult incremental_test_compile(Arena* arena, String8 target, String8 input, String8 output, String8 cache,
                                                                  String8 extra)
{
    String8 command[10];
    u32 count = 0;
    command[count++] = S8("-target");
    command[count++] = target;
    command[count++] = S8("-nostdinc");
    command[count++] = S8("-c");
    command[count++] = S8("-o");
    command[count++] = output;
    command[count++] = input;
    if (cache.length)
    {
        command[count++] = string_format(arena, S8("-fincremental-cache={S8}"), cache);
    }
    if (extra.length)
    {
        command[count++] = extra;
    }
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = count});
    return compiler_driver_execute_invocation(arena, invocation);
}

BUSTER_GLOBAL_LOCAL bool incremental_test_same_file(Arena* arena, String8 left, String8 right)
{
    ByteSlice a = file_read(arena, left, (FileReadOptions){0});
    ByteSlice b = file_read(arena, right, (FileReadOptions){0});
    return a.pointer && b.pointer && a.length == b.length && memory_compare(a.pointer, b.pointer, a.length);
}

BUSTER_GLOBAL_LOCAL String8 incremental_test_path(Arena* arena, String8 stem, u32 index, String8 suffix)
{
    return buster_test_temporary_path(arena, string_format(arena, S8("{S8}-{u32}"), stem, index), suffix);
}

BUSTER_GLOBAL_LOCAL UnitTestResult incremental_test_driver(UnitTestArguments* arguments, String8 target, u32 target_index)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    // Temporary names are fixed per process, so every target gets its own
    // unit and cache instead of inheriting the previous target's pack.
    String8 input = incremental_test_path(arena, S8("incremental-unit"), target_index, S8(".c"));
    String8 clean = incremental_test_path(arena, S8("incremental-clean"), target_index, S8(".o"));
    String8 cached = incremental_test_path(arena, S8("incremental-cached"), target_index, S8(".o"));
    String8 cache = incremental_test_path(arena, S8("incremental-cache"), target_index, S8(""));
    String8 disabled_cache = incremental_test_path(arena, S8("incremental-disabled"), target_index, S8(""));
    BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(incremental_test_source_v1)));
    CompilerDriverResult oracle = incremental_test_compile(arena, target, input, clean, (String8){0}, (String8){0});
    BUSTER_TEST_RAW(arguments, oracle.error == COMPILER_DRIVER_ERROR_NONE, oracle.diagnostic);
    BUSTER_TEST(arguments, !oracle.incremental.enabled);
    // Cold: nothing to reuse, everything captured.
    CompilerDriverResult cold = incremental_test_compile(arena, target, input, cached, cache, S8("-fincremental-stats"));
    BUSTER_TEST_RAW(arguments, cold.error == COMPILER_DRIVER_ERROR_NONE, cold.diagnostic);
    BUSTER_TEST(arguments, incremental_test_same_file(arena, clean, cached));
    BUSTER_TEST(arguments, cold.incremental.enabled);
    // A host that cannot identify its compiler image runs uncached; the
    // outputs above and below must still match, which is the whole contract.
    bool identified = cold.incremental.pack_statuses[INCREMENTAL_PACK_UNAVAILABLE] == 0;
    if (identified)
    {
        BUSTER_TEST(arguments, cold.incremental.pack_statuses[INCREMENTAL_PACK_ABSENT] == 1 && cold.incremental.units_published == 1);
        BUSTER_TEST(arguments, cold.incremental.captures[INCREMENTAL_CAPTURE_STORED] == INCREMENTAL_TEST_FUNCTIONS);
    }
    // Warm: every function reused, the object unchanged, the pack not rewritten.
    CompilerDriverResult warm = incremental_test_compile(arena, target, input, cached, cache, (String8){0});
    BUSTER_TEST_RAW(arguments, warm.error == COMPILER_DRIVER_ERROR_NONE, warm.diagnostic);
    BUSTER_TEST(arguments, incremental_test_same_file(arena, clean, cached));
    if (identified)
    {
        BUSTER_TEST(arguments, warm.incremental.lookups[INCREMENTAL_LOOKUP_REUSED] == INCREMENTAL_TEST_FUNCTIONS);
        BUSTER_TEST(arguments, warm.incremental.pack_bytes_written == 0 && warm.incremental.reused_code_bytes != 0);
    }
    // One body changed and every later line moved: exactly that function is
    // generated again, the rest replay against the new line numbers.
    BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(incremental_test_source_v2)));
    oracle = incremental_test_compile(arena, target, input, clean, (String8){0}, (String8){0});
    BUSTER_TEST_RAW(arguments, oracle.error == COMPILER_DRIVER_ERROR_NONE, oracle.diagnostic);
    CompilerDriverResult edited = incremental_test_compile(arena, target, input, cached, cache, (String8){0});
    BUSTER_TEST_RAW(arguments, edited.error == COMPILER_DRIVER_ERROR_NONE, edited.diagnostic);
    BUSTER_TEST(arguments, incremental_test_same_file(arena, clean, cached));
    if (identified)
    {
        BUSTER_TEST(arguments, edited.incremental.lookups[INCREMENTAL_LOOKUP_MISS_BODY] == 1);
        BUSTER_TEST(arguments, edited.incremental.lookups[INCREMENTAL_LOOKUP_REUSED] == INCREMENTAL_TEST_FUNCTIONS - 1);
    }
    // Verification compiles every hit again and compares artifacts.
    CompilerDriverResult verified = incremental_test_compile(arena, target, input, cached, cache, S8("-fincremental-verify"));
    BUSTER_TEST_RAW(arguments, verified.error == COMPILER_DRIVER_ERROR_NONE, verified.diagnostic);
    BUSTER_TEST(arguments, incremental_test_same_file(arena, clean, cached));
    if (identified)
    {
        BUSTER_TEST(arguments, verified.incremental.verified == INCREMENTAL_TEST_FUNCTIONS && !verified.incremental.verify_mismatches);
    }
    // Another context misses everything and still matches its own oracle.
    CompilerDriverResult oracle_g0 = incremental_test_compile(arena, target, input, clean, (String8){0}, S8("-g0"));
    CompilerDriverResult context = incremental_test_compile(arena, target, input, cached, cache, S8("-g0"));
    BUSTER_TEST(arguments, oracle_g0.error == COMPILER_DRIVER_ERROR_NONE && context.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, incremental_test_same_file(arena, clean, cached));
    if (identified)
    {
        BUSTER_TEST(arguments, context.incremental.lookups[INCREMENTAL_LOOKUP_MISS_CONTEXT] == INCREMENTAL_TEST_FUNCTIONS);
    }
    // Corrupt, truncated and foreign packs are ignored and replaced; the
    // object is still the clean one.
    String8 pack = incremental_pack_path(arena, cache, input);
    ByteSlice original = file_read(arena, pack, (FileReadOptions){0});
    if (identified && BUSTER_REQUIRE(arguments, original.pointer && original.length > 64))
    {
        String8 corruptions[] = {
            {.pointer = (char8*)original.pointer, .length = original.length / 2},
            S8("not a pack"),
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(corruptions); index += 1)
        {
            BUSTER_TEST(arguments, file_write(pack, BUSTER_SLICE_TO_BYTE_SLICE(corruptions[index])));
            CompilerDriverResult recovered = incremental_test_compile(arena, target, input, cached, cache, S8("-g0"));
            BUSTER_TEST(arguments, recovered.error == COMPILER_DRIVER_ERROR_NONE && incremental_test_same_file(arena, clean, cached));
            BUSTER_TEST(arguments, recovered.incremental.pack_statuses[INCREMENTAL_PACK_CORRUPT] == 1 && recovered.incremental.units_published == 1);
        }
        u8* flipped = arena_allocate(arena, u8, original.length);
        memcpy(flipped, original.pointer, original.length);
        flipped[original.length - 1] ^= 1;
        BUSTER_TEST(arguments, file_write(pack, (ByteSlice){.pointer = flipped, .length = original.length}));
        CompilerDriverResult recovered = incremental_test_compile(arena, target, input, cached, cache, (String8){0});
        oracle = incremental_test_compile(arena, target, input, clean, (String8){0}, (String8){0});
        BUSTER_TEST(arguments, recovered.error == COMPILER_DRIVER_ERROR_NONE && incremental_test_same_file(arena, clean, cached));
        BUSTER_TEST(arguments, recovered.incremental.pack_statuses[INCREMENTAL_PACK_CORRUPT] == 1);
    }
    // Disabled completely: the last flag wins and nothing is created.
    CompilerDriverResult disabled = incremental_test_compile(arena, target, input, cached, disabled_cache, S8("-fno-incremental-cache"));
    BUSTER_TEST(arguments, disabled.error == COMPILER_DRIVER_ERROR_NONE && !disabled.incremental.enabled);
    BUSTER_TEST(arguments, incremental_test_same_file(arena, clean, cached));
    BUSTER_TEST(arguments, !file_read(arena, incremental_pack_path(arena, disabled_cache, input), (FileReadOptions){0}).pointer);
    return result;
}

UnitTestResult incremental_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    UnitTestResult part = incremental_test_artifact_codec(arguments);
    result.succeeded_test_count += part.succeeded_test_count;
    result.test_count += part.test_count;
    part = incremental_test_pack_codec(arguments);
    result.succeeded_test_count += part.succeeded_test_count;
    result.test_count += part.test_count;
    // One target per object format and architecture, independent of the host.
    String8 targets[] = {S8("x86_64-unknown-linux"), S8("aarch64-unknown-linux"), S8("x86_64-pc-windows"), S8("aarch64-apple-macos")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(targets); index += 1)
    {
        part = incremental_test_driver(arguments, targets[index], index);
        result.succeeded_test_count += part.succeeded_test_count;
        result.test_count += part.test_count;
    }
    return result;
}
#endif
