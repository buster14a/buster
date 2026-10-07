// Exact-file inventory pilot (#2216), included by build.c before test_all.
// binary_coverage_inventory_parse independently derives nominal ELF PF_X
// PT_LOAD envelopes; these byte ranges are NOT instruction-site denominators.
// binary_coverage_report keeps every envelope unclassified and every execution
// metric unmeasured. binary_coverage_verify_inventory certifies only exact
// report equality, never execution, completeness, MC/DC or functional behavior.
// No collector is admitted: --evidence fails rather than trusting a claimed hit.
#define BINARY_COVERAGE_SCHEMA "buster-binary-coverage-inventory-v1"
#define BINARY_COVERAGE_MAX_PROGRAM_HEADERS 128

typedef struct BinaryCoverageRange BinaryCoverageRange;
struct BinaryCoverageRange
{
    u64 program_header_index;
    u64 file_offset;
    u64 file_bytes;
    u64 virtual_address;
    u64 memory_bytes;
    u32 flags;
};

typedef struct BinaryCoverageInventory BinaryCoverageInventory;
struct BinaryCoverageInventory
{
    BinaryCoverageRange* loads;
    u64 load_count;
    u64 executable_range_count;
    u64 executable_file_bytes;
    u64 executable_memory_bytes;
    u64 artifact_bytes;
    u64 entry_virtual_address;
    u16 elf_type;
    u8 osabi;
    String8 sha256;
};

// All reads follow explicit enclosing byte/table bounds checks. No native
// casts, alignment requirements, host endian assumptions or ELF dependencies.
BUSTER_GLOBAL_LOCAL u64 binary_coverage_le(u8* bytes, u64 width)
{
    u64 result = 0;
    for (u64 i = 0; i < width; i += 1)
    {
        result |= (u64)bytes[i] << (8 * i);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool binary_coverage_inside(u64 offset, u64 length, u64 capacity)
{
    bool result = offset <= capacity && length <= capacity - offset;
    return result;
}

BUSTER_GLOBAL_LOCAL bool binary_coverage_overlap(u64 left, u64 left_length, u64 right, u64 right_length)
{
    // Callers have already proved that both ends fit u64.
    bool result = left_length && right_length && left < right + right_length && right < left + left_length;
    return result;
}

BUSTER_GLOBAL_LOCAL bool binary_coverage_inventory_parse(Arena* arena, ByteSlice bytes, BinaryCoverageInventory* inventory)
{
    *inventory = (BinaryCoverageInventory){0};
    bool valid = bytes.pointer && bytes.length >= 64;
    u64 phoff = 0, shoff = 0, phnum = 0, shnum = 0;
    if (valid)
    {
        u8* h = bytes.pointer;
        phoff = binary_coverage_le(h + 32, 8);
        shoff = binary_coverage_le(h + 40, 8);
        phnum = binary_coverage_le(h + 56, 2);
        shnum = binary_coverage_le(h + 60, 2);
        inventory->elf_type = (u16)binary_coverage_le(h + 16, 2);
        inventory->entry_virtual_address = binary_coverage_le(h + 24, 8);
        inventory->osabi = h[7];
        valid = h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F' && h[4] == 2 && h[5] == 1 &&
            h[6] == 1 && (h[7] == 0 || h[7] == 3) && h[8] == 0 &&
            (inventory->elf_type == 2 || inventory->elf_type == 3) && binary_coverage_le(h + 18, 2) == 62 &&
            binary_coverage_le(h + 20, 4) == 1 && binary_coverage_le(h + 52, 2) == 64 &&
            binary_coverage_le(h + 54, 2) == 56 && phnum && phnum <= BINARY_COVERAGE_MAX_PROGRAM_HEADERS &&
            phoff >= 64 && binary_coverage_inside(phoff, phnum * 56, bytes.length);
        for (u64 i = 9; valid && i < 16; i += 1)
        {
            valid = h[i] == 0;
        }
        // PN_XNUM/SHN_XINDEX and extended section counts are unsupported.
        // Sections are checked structurally, but never establish the envelope.
        u64 shstr = binary_coverage_le(h + 62, 2);
        valid = valid && shnum < 0xff00 && shstr < 0xff00 && ((!shnum && !shoff && !shstr) ||
            (shnum && shoff >= 64 && binary_coverage_le(h + 58, 2) == 64 && shstr < shnum &&
             binary_coverage_inside(shoff, shnum * 64, bytes.length) &&
             !binary_coverage_overlap(phoff, phnum * 56, shoff, shnum * 64)));
    }
    if (valid)
    {
        inventory->loads = arena_allocate(arena, BinaryCoverageRange, phnum);
        for (u64 i = 0; valid && i < phnum; i += 1)
        {
            u8* p = bytes.pointer + phoff + i * 56;
            u64 type = binary_coverage_le(p, 4);
            u64 offset = binary_coverage_le(p + 8, 8);
            u64 file_bytes = binary_coverage_le(p + 32, 8);
            // Other header kinds cannot establish this pilot's executable
            // envelope. In particular, all PT_NULL members are undefined.
            if (type == 1)
            {
                BinaryCoverageRange range = {.program_header_index = i, .file_offset = offset, .file_bytes = file_bytes,
                    .virtual_address = binary_coverage_le(p + 16, 8), .memory_bytes = binary_coverage_le(p + 40, 8),
                    .flags = (u32)binary_coverage_le(p + 4, 4)};
                u64 align = binary_coverage_le(p + 48, 8);
                valid = binary_coverage_inside(offset, file_bytes, bytes.length) && range.file_bytes <= range.memory_bytes &&
                    binary_coverage_inside(range.virtual_address, range.memory_bytes, ~(u64)0) &&
                    (align <= 1 || (!(align & (align - 1)) && range.file_offset % align == range.virtual_address % align)) &&
                    (!inventory->load_count || range.virtual_address >= inventory->loads[inventory->load_count - 1].virtual_address);
                for (u64 j = 0; valid && j < inventory->load_count; j += 1)
                {
                    BinaryCoverageRange other = inventory->loads[j];
                    // Reject overlaps involving executable loads: ordering and
                    // permission aliases would make this nominal inventory ambiguous.
                    if ((range.flags | other.flags) & 1)
                    {
                        valid = !binary_coverage_overlap(range.virtual_address, range.memory_bytes, other.virtual_address, other.memory_bytes) &&
                            !binary_coverage_overlap(range.file_offset, range.file_bytes, other.file_offset, other.file_bytes);
                    }
                }
                if (valid)
                {
                    inventory->loads[inventory->load_count] = range;
                    inventory->load_count += 1;
                    if ((range.flags & 1) && range.memory_bytes)
                    {
                        valid = range.file_bytes <= ~(u64)0 - inventory->executable_file_bytes &&
                            range.memory_bytes <= ~(u64)0 - inventory->executable_memory_bytes;
                        if (valid)
                        {
                            inventory->executable_range_count += 1;
                            inventory->executable_file_bytes += range.file_bytes;
                            inventory->executable_memory_bytes += range.memory_bytes;
                        }
                    }
                }
            }
        }
    }
    valid = valid && inventory->executable_range_count;
    if (valid)
    {
        inventory->artifact_bytes = bytes.length;
        inventory->sha256 = stage_object_sha256_bytes(arena, bytes.pointer, bytes.length);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 binary_coverage_report(Arena* arena, String8 path, BinaryCoverageInventory* inventory)
{
    String8List parts = {0};
    string8_list_push(arena, &parts, string_format(arena, S8("{{\n  \"schema\": \"" BINARY_COVERAGE_SCHEMA "\",\n"
        "  \"scope\": \"one-explicit-artifact\",\n  \"artifact_count\": 1,\n"
        "  \"artifact\": {{\"path\": {S8}, \"sha256\": \"{S8}\", \"bytes\": {u64}, \"format\": \"ELF64-LE-x86_64\", \"elf_type\": {u64}, "
        "\"osabi\": {u64}, \"entry_virtual_address\": {u64}}},\n"
        "  \"provenance\": {{\"source_revision\": null, \"source_tree\": null, \"toolchain\": null, \"configuration\": null}},\n"
        "  \"envelope_basis\": \"nominal-PF_X-PT_LOAD-memory-ranges\",\n"
        "  \"executable_range_count\": {u64},\n  \"unclassified_file_bytes\": {u64},\n  \"unclassified_memory_bytes\": {u64},\n"
        "  \"executable_ranges\": [\n"), matrix_coverage_json_escape(arena, path), inventory->sha256, inventory->artifact_bytes,
        (u64)inventory->elf_type, (u64)inventory->osabi, inventory->entry_virtual_address,
        inventory->executable_range_count, inventory->executable_file_bytes, inventory->executable_memory_bytes));
    u64 emitted = 0;
    for (u64 i = 0; i < inventory->load_count; i += 1)
    {
        BinaryCoverageRange r = inventory->loads[i];
        if ((r.flags & 1) && r.memory_bytes)
        {
            emitted += 1;
            string8_list_push(arena, &parts, string_format(arena, S8("    {{\"program_header_index\": {u64}, \"file_offset\": {u64}, \"file_bytes\": {u64}, "
                "\"virtual_address\": {u64}, \"memory_bytes\": {u64}, \"flags\": {u64}, \"zero_fill_virtual_address\": {u64}, "
                "\"zero_fill_bytes\": {u64}, \"classification\": \"unknown-executable-bytes\"}}{S8}\n"),
                r.program_header_index, r.file_offset, r.file_bytes, r.virtual_address, r.memory_bytes, (u64)r.flags,
                r.virtual_address + r.file_bytes, r.memory_bytes - r.file_bytes, emitted < inventory->executable_range_count ? S8(",") : S8("")));
        }
    }
    string8_list_push(arena, &parts, S8("  ],\n"
        "  \"instruction_sites\": {\"status\": \"unmeasured\", \"denominator\": null, \"executed\": null, \"percentage\": null},\n"
        "  \"machine_branch_outcomes\": {\"status\": \"unmeasured\", \"denominator\": null, \"executed\": null, \"percentage\": null},\n"
        "  \"mcdc\": {\"status\": \"unmeasured\", \"obligations\": null, \"independence_pairs\": null, \"percentage\": null},\n"
        "  \"functional_assertions\": {\"status\": \"unmeasured\", \"assertions\": null, \"passed\": null, \"percentage\": null},\n"
        "  \"collection\": {\"approved_collector\": null, \"complete\": false},\n"
        "  \"gaps\": [\"instruction-boundaries-unclassified\", \"no-approved-exact-execution-collector\", "
        "\"live-mappings-and-loader-permissions-uncollected\", \"external-and-system-artifacts-uncollected\", "
        "\"dynamic-and-generated-executable-mappings-uncollected\", \"non-load-program-header-semantics-unvalidated\", \"mcdc-companion-unavailable\", "
        "\"mutable-file-snapshot-and-runtime-continuity-unverified\", \"source-tree-toolchain-configuration-unverified\"],\n"
        "  \"overall_status\": \"incomplete\",\n  \"success_means\": \"inventory-report-created-only\"\n}\n"));
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, parts), false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool binary_coverage_verify_inventory(Arena* arena, ByteSlice artifact, String8 path, String8 received)
{
    BinaryCoverageInventory independent = {0};
    bool result = binary_coverage_inventory_parse(arena, artifact, &independent) &&
        string_equal(binary_coverage_report(arena, path, &independent), received);
    return result;
}

BUSTER_GLOBAL_LOCAL void binary_coverage_expect(bool condition, String8 name, u64* checks, u64* failures)
{
    *checks += 1;
    if (!condition)
    {
        *failures += 1;
        string_print(S8("BINARY_COVERAGE_SELF_TEST_FAILURE {S8}\n"), name);
    }
}

// Literal bytes authored from the ELF ABI layout, not serialized by the parser.
// SHA-256 golden independently calculated with Python hashlib during preparation.
BUSTER_GLOBAL_LOCAL u8 binary_coverage_fixture[] = {
    0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x3e, 0x00, 0x01, 0x00, 0x00, 0x00, 0x80, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x38, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xc3, 0x90, 0x0f, 0x0b,
};

BUSTER_GLOBAL_LOCAL bool binary_coverage_self_test(Arena* arena)
{
    u64 checks = 0, failures = 0;
    ByteSlice artifact = {.pointer = binary_coverage_fixture, .length = sizeof(binary_coverage_fixture)};
    String8 path = S8("fixture/native-driver");
    BinaryCoverageInventory inventory = {0};
    bool valid = binary_coverage_inventory_parse(arena, artifact, &inventory);
    binary_coverage_expect(valid, S8("literal-ELF-accepted"), &checks, &failures);
    if (valid)
    {
        BinaryCoverageRange range = inventory.loads[0];
        binary_coverage_expect(inventory.artifact_bytes == 132 && inventory.load_count == 1 && inventory.executable_range_count == 1 &&
            inventory.executable_file_bytes == 4 && inventory.executable_memory_bytes == 6 && inventory.entry_virtual_address == 4194432 &&
            inventory.osabi == 0 && range.file_offset == 128 &&
            range.virtual_address == 4194432 && range.file_bytes == 4 && range.memory_bytes == 6 && range.flags == 5,
            S8("independent-envelope-golden-with-zero-fill"), &checks, &failures);
        binary_coverage_expect(string_equal(inventory.sha256, S8("fc643715370b74e9959f12ac5e491061edabcb1f770c5ebfbf969ae7ad548684")),
            S8("independent-hash-golden"), &checks, &failures);
        String8 report = binary_coverage_report(arena, path, &inventory);
        binary_coverage_expect(binary_coverage_verify_inventory(arena, artifact, path, report), S8("inventory-equality-only"), &checks, &failures);
        binary_coverage_expect(string_first_sequence(report, S8("\"zero_fill_virtual_address\": 4194436, \"zero_fill_bytes\": 2")) != BUSTER_STRING_NO_MATCH &&
            string_first_sequence(report, S8("\"functional_assertions\": {\"status\": \"unmeasured\", \"assertions\": null, \"passed\": null, \"percentage\": null}")) != BUSTER_STRING_NO_MATCH &&
            string_first_sequence(report, S8("\"approved_collector\": null, \"complete\": false")) != BUSTER_STRING_NO_MATCH,
            S8("independent-report-fields-remain-unknown"), &checks, &failures);
        struct {String8 name; String8 before; String8 after;} controls[] = {
            {S8("omitted-artifact"), S8("\"artifact_count\": 1"), S8("\"artifact_count\": 0")},
            {S8("omitted-range"), S8("\"executable_range_count\": 1"), S8("\"executable_range_count\": 0")},
            {S8("omitted-range-byte"), S8("\"file_bytes\": 4"), S8("\"file_bytes\": 3")},
            {S8("wrong-build-digest"), inventory.sha256, S8("0000000000000000000000000000000000000000000000000000000000000000")},
            {S8("uncovered-instruction-claim"), S8("\"instruction_sites\": {\"status\": \"unmeasured\", \"denominator\": null, \"executed\": null, \"percentage\": null}"),
                S8("\"instruction_sites\": {\"status\": \"complete\", \"denominator\": 4, \"executed\": 3}")},
            {S8("uncovered-edge-claim"), S8("\"machine_branch_outcomes\": {\"status\": \"unmeasured\", \"denominator\": null, \"executed\": null, \"percentage\": null}"),
                S8("\"machine_branch_outcomes\": {\"status\": \"complete\", \"denominator\": 2, \"executed\": 1}")},
            {S8("missing-mcdc-pair-claim"), S8("\"mcdc\": {\"status\": \"unmeasured\", \"obligations\": null, \"independence_pairs\": null, \"percentage\": null}"),
                S8("\"mcdc\": {\"status\": \"complete\", \"obligations\": 2, \"independence_pairs\": 1}")},
            {S8("incomplete-collection-claim"), S8("\"complete\": false"), S8("\"complete\": true")},
            {S8("self-declared-100-percent"), S8("\"overall_status\": \"incomplete\""), S8("\"overall_status\": \"complete\", \"coverage\": 100")},
        };
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(controls); i += 1)
        {
            u64 at = string_first_sequence(report, controls[i].before);
            bool rejected = false;
            if (at != BUSTER_STRING_NO_MATCH)
            {
                String8 changed = string_format(arena, S8("{S8}{S8}{S8}"), string_slice(report, 0, at), controls[i].after,
                    string_slice(report, at + controls[i].before.length, report.length));
                rejected = !binary_coverage_verify_inventory(arena, artifact, path, changed);
            }
            binary_coverage_expect(rejected, controls[i].name, &checks, &failures);
        }
        // A second build with unchanged envelopes must still reject the first
        // report. This is stronger than editing a digest field in a report.
        u8 changed_bytes[sizeof(binary_coverage_fixture)];
        memcpy(changed_bytes, binary_coverage_fixture, sizeof(changed_bytes));
        changed_bytes[131] = 0x90;
        ByteSlice changed_artifact = {.pointer = changed_bytes, .length = sizeof(changed_bytes)};
        binary_coverage_expect(!binary_coverage_verify_inventory(arena, changed_artifact, path, report),
            S8("wrong-build-identical-envelope"), &checks, &failures);
    }
    // Truncate independently at every byte boundary, including the ELF header,
    // the program-header table and the executable payload.
    for (u64 length = 0; length < sizeof(binary_coverage_fixture); length += 1)
    {
        ByteSlice truncated = {.pointer = binary_coverage_fixture, .length = length};
        binary_coverage_expect(!binary_coverage_inventory_parse(arena, truncated, &inventory),
            string_format(arena, S8("truncated-at-{u64}"), length), &checks, &failures);
    }
    struct {u64 offset; u8 value;} malformed[] = {
        {0, 0}, {4, 1}, {5, 2}, {7, 9}, {8, 1}, {9, 1}, {16, 1}, {18, 183}, {20, 2}, {32, 0xff},
        {39, 0xff}, {40, 64}, {52, 63}, {54, 55}, {56, 0}, {56, 129}, {57, 0xff}, {62, 0xff}, {63, 0xff},
        {68, 4}, {72, 0xff}, {79, 0xff}, {80, 0x81}, {96, 7}, {104, 3}, {112, 3},
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(malformed); i += 1)
    {
        u8 changed[sizeof(binary_coverage_fixture)];
        memcpy(changed, binary_coverage_fixture, sizeof(changed));
        changed[malformed[i].offset] = malformed[i].value;
        binary_coverage_expect(!binary_coverage_inventory_parse(arena, (ByteSlice){.pointer = changed, .length = sizeof(changed)}, &inventory),
            string_format(arena, S8("malformed-field-{u64}"), i), &checks, &failures);
    }
    // Explicit all-ones address plus six bytes proves virtual-end overflow.
    u8 variant[sizeof(binary_coverage_fixture)];
    memcpy(variant, binary_coverage_fixture, sizeof(variant));
    memset(variant + 80, 0xff, 8);
    binary_coverage_expect(!binary_coverage_inventory_parse(arena, (ByteSlice){.pointer = variant, .length = sizeof(variant)}, &inventory),
        S8("virtual-range-overflow"), &checks, &failures);
    memcpy(variant, binary_coverage_fixture, sizeof(variant));
    variant[96] = 0;
    valid = binary_coverage_inventory_parse(arena, (ByteSlice){.pointer = variant, .length = sizeof(variant)}, &inventory);
    binary_coverage_expect(valid && inventory.executable_file_bytes == 0 && inventory.executable_memory_bytes == 6,
        S8("memory-only-executable-load-retained"), &checks, &failures);
    memcpy(variant, binary_coverage_fixture, sizeof(variant));
    variant[16] = 3;
    variant[7] = 3;
    binary_coverage_expect(binary_coverage_inventory_parse(arena, (ByteSlice){.pointer = variant, .length = sizeof(variant)}, &inventory),
        S8("ET_DYN-Linux-OSABI-accepted"), &checks, &failures);
    // Two independent program-header records, with distinct byte/virtual
    // envelopes, make range omissions and permission aliases observable.
    u8 multiple[256] = {0};
    memcpy(multiple, binary_coverage_fixture, 64);
    memcpy(multiple + 64, binary_coverage_fixture + 64, 56);
    memcpy(multiple + 120, binary_coverage_fixture + 64, 56);
    multiple[56] = 2;
    multiple[72] = 240;
    multiple[80] = 240;
    multiple[128] = 248;
    multiple[136] = 0;
    multiple[137] = 1;
    multiple[152] = 2;
    multiple[160] = 2;
    multiple[168] = 8;
    memcpy(multiple + 240, binary_coverage_fixture + 128, 4);
    multiple[248] = 0x90;
    multiple[249] = 0xc3;
    ByteSlice multiple_artifact = {.pointer = multiple, .length = sizeof(multiple)};
    valid = binary_coverage_inventory_parse(arena, multiple_artifact, &inventory);
    binary_coverage_expect(valid && inventory.executable_range_count == 2 && inventory.executable_file_bytes == 6 &&
        inventory.executable_memory_bytes == 8, S8("multiple-executable-envelopes"), &checks, &failures);
    if (valid)
    {
        String8 report = binary_coverage_report(arena, path, &inventory);
        u64 second = string_first_sequence(report, S8("    {\"program_header_index\": 1,"));
        u64 end = string_first_sequence(report, S8("\n  ],"));
        bool omitted_rejected = false;
        if (second != BUSTER_STRING_NO_MATCH && end != BUSTER_STRING_NO_MATCH && second > 2 && end > second)
        {
            String8 omitted = string_format(arena, S8("{S8}{S8}"), string_slice(report, 0, second - 2), string_slice(report, end, report.length));
            omitted_rejected = !binary_coverage_verify_inventory(arena, multiple_artifact, path, omitted);
        }
        binary_coverage_expect(omitted_rejected, S8("omitted-entire-second-range"), &checks, &failures);
    }
    u8 ignored_null[sizeof(multiple)];
    memcpy(ignored_null, multiple, sizeof(ignored_null));
    memset(ignored_null + 120, 0xff, 56);
    memset(ignored_null + 120, 0, 4);
    valid = binary_coverage_inventory_parse(arena, (ByteSlice){.pointer = ignored_null, .length = sizeof(ignored_null)}, &inventory);
    binary_coverage_expect(valid && inventory.executable_range_count == 1, S8("PT_NULL-undefined-members-ignored"), &checks, &failures);
    multiple[136] = 224;
    multiple[137] = 0;
    binary_coverage_expect(!binary_coverage_inventory_parse(arena, multiple_artifact, &inventory),
        S8("load-virtual-address-order-rejected"), &checks, &failures);
    multiple[136] = 240;
    multiple[137] = 0;
    binary_coverage_expect(!binary_coverage_inventory_parse(arena, multiple_artifact, &inventory),
        S8("overlapping-executable-virtual-ranges"), &checks, &failures);
    multiple[124] = 4;
    binary_coverage_expect(!binary_coverage_inventory_parse(arena, multiple_artifact, &inventory),
        S8("executable-nonexecutable-permission-alias"), &checks, &failures);
    multiple[124] = 5;
    multiple[136] = 0;
    multiple[137] = 1;
    multiple[128] = 240;
    binary_coverage_expect(!binary_coverage_inventory_parse(arena, multiple_artifact, &inventory),
        S8("overlapping-executable-file-ranges"), &checks, &failures);
    bool result = failures == 0;
    string_print(S8("BINARY_COVERAGE_SELF_TEST checks={u64} failures={u64} status={S8}\n"), checks, failures, result ? S8("pass") : S8("fail"));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult binary_coverage_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test")))
    {
        result = binary_coverage_self_test(arena) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if ((arguments.length == 1 || (arguments.length == 3 && string_equal(arguments.pointer[1], S8("--verify-inventory")))) &&
             arguments.pointer[0].length && arguments.pointer[0].pointer[0] != '-')
    {
        String8 path = os_path_absolute(arena, arguments.pointer[0], true);
        FileMapRead artifact = file_map_read(arena, path, (FileReadOptions){.map_required = 1});
        BinaryCoverageInventory inventory = {0};
        bool valid = artifact.mapped_pointer && binary_coverage_inventory_parse(arena, artifact.bytes, &inventory);
        if (valid && arguments.length == 3)
        {
            FileMapRead received = file_map_read(arena, arguments.pointer[2], (FileReadOptions){.map_required = 1});
            String8 report = {.pointer = (char8*)received.bytes.pointer, .length = received.bytes.length};
            valid = received.mapped_pointer && binary_coverage_verify_inventory(arena, artifact.bytes, path, report);
            file_map_unmap(received);
        }
        if (valid)
        {
            string_print(S8("{S8}"), binary_coverage_report(arena, path, &inventory));
            result = PROCESS_RESULT_SUCCESS;
        }
        else
        {
            string_print(S8("error: binary_coverage_inventory: unreadable, malformed, unsupported or mismatched artifact/inventory; no execution claim accepted\n"));
        }
        file_map_unmap(artifact);
    }
    else
    {
        string_print(S8("usage: ./build.sh binary_coverage_inventory ARTIFACT [--verify-inventory REPORT]\n"
            "       ./build.sh binary_coverage_inventory --self-test\n"
            "error: --evidence is unsupported: no approved exact-execution collector; inventory success never certifies coverage\n"));
    }
    return result;
}
