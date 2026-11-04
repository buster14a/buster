// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// #1255: dynamic_test.c includes this after its image readers and lane helpers. The fixture takes every image-bound
// address at runtime; the expected output comes from the ELF headers, not the writer's symbol-value implementation.
// With --cross-check, GNU ld and lld run the same fixture against those rules. GNU ld checks all twelve names;
// lld omits __etext, a GNU alias it does not define. The direct linker still checks all twelve in every write.

static const char* const dynamic_bounds_names[] = {
    "__ehdr_start",
    "__executable_start",
    "__preinit_array_start",
    "__preinit_array_end",
    "_etext",
    "etext",
    "__etext",
    "_edata",
    "edata",
    "__bss_start",
    "_end",
    "end",
};

static const char dynamic_bounds_source[] =
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "extern char __ehdr_start[], __executable_start[];\n"
    "extern char __preinit_array_start[], __preinit_array_end[];\n"
    "extern char _etext[], etext[], __etext[], _edata[], edata[], __bss_start[], _end[], end[];\n"
    "char payload[37] = { 1 };\n"
    "__attribute__((aligned(64))) char zeroes[73];\n"
    "int main(void)\n"
    "{\n"
    "    char* addresses[] = { __ehdr_start, __executable_start, __preinit_array_start, __preinit_array_end,\n"
    "                          _etext, etext,\n"
    "#if !BUSTER_DYNAMIC_BOUNDS_NO_ETEXT_ALIAS\n"
    "                          __etext,\n"
    "#endif\n"
    "                          _edata, edata, __bss_start, _end, end };\n"
    "    for (unsigned int i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i += 1)\n"
    "    {\n"
    "        printf(\"%llu\\n\", (unsigned long long)((uintptr_t)addresses[i] - (uintptr_t)__ehdr_start));\n"
    "    }\n"
    "    return payload[0] != 1 || zeroes[72] != 0;\n"
    "}\n";

// The direct writer follows GNU ld for empty preinit bounds: both coincide at .init_array's start. Lld uses offset
// zero when .preinit_array is absent. A nonempty .data and aligned .bss distinguish the file and memory bounds.
static bool dynamic_bounds_values(DynamicTestTally* tally, String8 file, uint64_t* values, bool gnu_empty_preinit)
{
    const DynamicTestImage image = dynamic_test_image(file);
    const Elf64_Shdr* init = dynamic_test_section(&image, ".init_array");
    const Elf64_Shdr* bss = dynamic_test_section(&image, ".bss");
    const Elf64_Shdr* preinit = dynamic_test_section(&image, ".preinit_array");
    uint64_t base = UINT64_MAX;
    uint64_t text_end = 0;
    uint64_t data_end = 0;
    uint64_t image_end = 0;
    for (uint32_t i = 0; image.valid && i < image.header->e_phnum; i += 1)
    {
        const Elf64_Phdr* segment = &image.program_headers[i];
        const bool load = segment->p_type == PT_LOAD;
        const uint64_t end = segment->p_vaddr + segment->p_memsz;
        base = load && segment->p_offset == 0 ? segment->p_vaddr : base;
        text_end = load && (segment->p_flags & PF_X) != 0 && end > text_end ? end : text_end;
        image_end = load && end > image_end ? end : image_end;
    }
    for (uint32_t i = 1; image.valid && i < image.header->e_shnum; i += 1)
    {
        const Elf64_Shdr* section = &image.section_headers[i];
        const bool bytes = (section->sh_flags & SHF_ALLOC) != 0 && section->sh_type != SHT_NOBITS;
        const uint64_t end = section->sh_addr + section->sh_size;
        data_end = bytes && end > data_end ? end : data_end;
    }
    const bool valid = image.valid && base != UINT64_MAX && init != 0 && bss != 0 && bss->sh_size != 0 &&
                       (preinit == 0 || preinit->sh_size == 0) && text_end > base && data_end >= text_end &&
                       bss->sh_addr >= data_end && image_end >= bss->sh_addr + bss->sh_size;
    dynamic_test_expect(tally, valid, "ELF headers describe the fixture's image bounds", 0);
    if (valid)
    {
        values[0] = 0;
        values[1] = 0;
        values[2] = preinit != 0 ? preinit->sh_addr - base : gnu_empty_preinit ? init->sh_addr - base : 0;
        values[3] = values[2];
        values[4] = text_end - base;
        values[5] = values[4];
        values[6] = values[4];
        values[7] = data_end - base;
        values[8] = values[7];
        values[9] = bss->sh_addr - base;
        values[10] = image_end - base;
        values[11] = values[10];
    }
    return valid;
}

static uint64_t dynamic_bounds_output(const uint64_t* values, uint8_t* output, uint64_t capacity, bool text_alias)
{
    uint64_t length = 0;
    bool valid = true;
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(dynamic_bounds_names); i += 1)
    {
        if (text_alias || strcmp(dynamic_bounds_names[i], "__etext") != 0)
        {
            const int written = snprintf((char*)output + length, capacity - length, "%llu\n", (unsigned long long)values[i]);
            valid = written >= 0 && (uint64_t)written < capacity - length;
            length += valid ? (uint64_t)written : 0;
        }
    }
    return valid ? length : 0;
}

// Every write needs its own expected addresses: replacing RELR with RELA changes the layout. Compare both the
// binding's references and the running program with that file's headers, including the exact and RELA-only writes.
static void dynamic_bounds_check(DynamicTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, String8 file, const char* path, bool relr,
                                 uint64_t index)
{
    uint64_t values[BUSTER_ARRAY_LENGTH(dynamic_bounds_names)] = { 0 };
    uint8_t expected[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
    const bool valid = dynamic_bounds_values(tally, file, values, true);
    const uint64_t length = valid ? dynamic_bounds_output(values, expected, sizeof(expected), true) : 0;
    if (valid)
    {
        dynamic_test_structure(tally, link, bind, runtime, file, relr, index);
        for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(dynamic_bounds_names); n += 1)
        {
            bool found = false;
            for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
            {
                if (executable_named(link, symbol, dynamic_bounds_names[n]))
                {
                    const bool resolved = link->symbols.source[symbol] == BUSTER_LINK_SOURCE_LINKER && link->symbols.reference[symbol] == values[n];
                    dynamic_test_expect(tally, resolved, dynamic_bounds_names[n], symbol);
                    found = true;
                }
            }
            dynamic_test_expect(tally, found, dynamic_bounds_names[n], n);
        }
    }
    dynamic_test_expect(tally, valid && length != 0 && executable_test_runs(path, expected, length), "the direct PIE runs with every bound at its ELF address", index);
}

// The kind mask is independently specified here. In particular, adding dynamic executables must not classify them as
// static runtimes, change shared-library admission, or enable the static executable's IPLT bounds in either PIE kind.
static void dynamic_bounds_kinds(DynamicTestTally* tally)
{
    for (uint32_t k = 0; k < RUNTIME_KINDS; k += 1)
    {
        const RuntimeKind kind = (RuntimeKind)k;
        const bool executable = kind == RUNTIME_KIND_DYNAMIC || kind == RUNTIME_KIND_STATIC || kind == RUNTIME_KIND_STATIC_PIE;
        for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(dynamic_bounds_names); n += 1)
        {
            const char* name = dynamic_bounds_names[n];
            const bool defined = runtime_linker_name(kind, (const uint8_t*)name, (uint32_t)strlen(name));
            dynamic_test_expect(tally, defined == executable, name, k);
        }
        dynamic_test_expect(tally, runtime_kind_is_static(kind) == (kind == RUNTIME_KIND_STATIC || kind == RUNTIME_KIND_STATIC_PIE), "static runtime classification unchanged", k);
        static const char* const iplt_names[] = {
            "__rela_iplt_start",
            "__rela_iplt_end",
        };
        for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(iplt_names); n += 1)
        {
            const char* name = iplt_names[n];
            const bool defined = runtime_linker_name(kind, (const uint8_t*)name, (uint32_t)strlen(name));
            dynamic_test_expect(tally, defined == (kind == RUNTIME_KIND_STATIC), name, k);
        }
    }
}

static bool dynamic_test_image_bounds(const ExecutableTestDirectory* directory, const Runtime* runtime, bool cross_check)
{
    DynamicTestTally tally = {
        .check = "dynamic image bounds",
    };
    dynamic_bounds_kinds(&tally);
    Scratch scratch = scratch_begin(0);
    char source_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char object_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char reference_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char out[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char err[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snprintf(source_path, sizeof(source_path), "%s/image-bounds.c", directory->path);
    snprintf(object_path, sizeof(object_path), "%s/image-bounds.o", directory->path);
    snprintf(out, sizeof(out), "%s/image-bounds.out", directory->path);
    snprintf(err, sizeof(err), "%s/image-bounds.err", directory->path);
    const String8 path = executable_test_path(directory, "image-bounds", path_bytes);
    const bool written_source = bind_test_write_file(source_path, dynamic_bounds_source, sizeof(dynamic_bounds_source) - 1);
    char* compile[] = { "cc", "-O2", "-fPIE", "-c", "-o", object_path, source_path, 0 };
    const bool compiled = written_source && object_test_run(compile, out, err) == 0;
    dynamic_test_expect(&tally, compiled, "cc compiles all twelve references", 0);
    const String8 object = compiled ? object_test_read_file(scratch.arena, object_path) : (String8){ 0 };
    BindTestLink bound = { 0 };
    const bool imported = object.pointer != 0 && bind_test_static_import(&bound, runtime, object);
    const bool begun = imported && bind_begin(&bound.bind, &bound.link, runtime, BIND_PATH_VECTOR, &bound.bytes, 1, &bound.arena);
    const bool ended = begun && bind_end(&bound.bind, &bound.link);
    const bool linked = ended && link_check(&bound.link) == LINK_ERROR_NONE;
    dynamic_test_expect(&tally, linked, "all twelve references bind in a dynamic executable", 0);
    ExecutableResult result = { 0 };
    String8 file = { 0 };
    const bool wrote = linked && dynamic_test_lanes(&tally, &bound.link, &bound.bind, runtime, path, "image bounds", &result, &file, directory->id_order, scratch.arena);
    if (wrote)
    {
        dynamic_bounds_check(&tally, &bound.link, &bound.bind, runtime, file, path.pointer, dynamic_test_relr_supported(runtime), 0);
    }
    const uint8_t* saved = wrote ? executable_test_save(&bound.link, scratch.arena) : 0;
    dynamic_test_expect(&tally, saved != 0, "room for the fixture's unpatched bytes", 0);
    static const uint32_t variant_lanes[] = { 1, 8, 1 };
    for (uint32_t w = 0; saved != 0 && w < BUSTER_ARRAY_LENGTH(variant_lanes); w += 1)
    {
        executable_test_restore(&bound.link, saved);
        DynamicTestWrite write = {
            .exact = w < 2,
            .rela_only = w == 2,
        };
        ExecutableResult variant = { 0 };
        const bool written = executable_test_reset(&bound.link, path.pointer) &&
                             dynamic_test_write_with(&write, &bound.link, &bound.bind, runtime, path, variant_lanes[w], &variant);
        dynamic_test_expect(&tally, written && (!write.rela_only || variant.relr_words == 0), "the exact or RELA-only write succeeds", w);
        const String8 bytes = written ? executable_test_read(scratch.arena, path.pointer) : (String8){ 0 };
        executable_test_restore(&bound.link, saved);
        if (written)
        {
            const bool identical = bytes.length == file.length && memcmp(bytes.pointer, file.pointer, file.length) == 0;
            dynamic_test_expect(&tally, !write.exact || identical, "the exact twins write the same file", variant_lanes[w]);
            dynamic_bounds_check(&tally, &bound.link, &bound.bind, runtime, bytes, path.pointer, !write.rela_only && dynamic_test_relr_supported(runtime), w + 1);
        }
    }
    uint64_t values[BUSTER_ARRAY_LENGTH(dynamic_bounds_names)] = { 0 };
    uint8_t expected[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
    static const char* const linkers[] = {
        "-fuse-ld=bfd",
        "-fuse-ld=lld",
    };
    for (uint32_t l = 0; cross_check && compiled && l < BUSTER_ARRAY_LENGTH(linkers); l += 1)
    {
        snprintf(reference_path, sizeof(reference_path), "%s/image-bounds-reference-%u", directory->path, l);
        char* gnu[] = { "cc", "-pie", (char*)linkers[l], "-o", reference_path, object_path, 0 };
        char* lld[] = { "cc", "-O2", "-fPIE", "-DBUSTER_DYNAMIC_BOUNDS_NO_ETEXT_ALIAS=1", "-pie", (char*)linkers[l], "-o", reference_path, source_path, 0 };
        const bool referenced = object_test_run(l == 0 ? gnu : lld, out, err) == 0;
        dynamic_test_expect(&tally, referenced, linkers[l], l);
        const String8 reference_file = referenced ? object_test_read_file(scratch.arena, reference_path) : (String8){ 0 };
        const bool reference_valid = referenced && dynamic_bounds_values(&tally, reference_file, values, l == 0);
        const uint64_t reference_length = reference_valid ? dynamic_bounds_output(values, expected, sizeof(expected), l == 0) : 0;
        const bool ran = reference_valid && reference_length != 0 && executable_test_runs(reference_path, expected, reference_length);
        dynamic_test_expect(&tally, ran, "the reference PIE runs with the same ELF-bound rules", l);
        if (!directory->keep)
        {
            unlink(reference_path);
        }
    }
    if (imported)
    {
        bind_test_link_end(&bound);
    }
    scratch_end(scratch);
    if (!directory->keep)
    {
        unlink(source_path);
        unlink(object_path);
        unlink(path.pointer);
        unlink(out);
        unlink(err);
    }
    return dynamic_test_report(&tally);
}
