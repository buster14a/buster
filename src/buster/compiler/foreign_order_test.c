// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Foreign's hash partitions assign lookup IDs, never output positions (#734/#153). Original C objects and an
// assembly entry exercise GOT relocations, archive extraction and executable exports referenced by a DSO. Changing
// partition_records or adding an unused archive member must change IDs without changing any output byte. Include
// after the foreign conversion/dynamic helpers; no C-driver or deferred-reader ID-order fixtures are involved.

#define BUSTER_FOREIGN_ORDER_TEST_SYMBOLS 301
#define BUSTER_FOREIGN_ORDER_TEST_UNUSED 4096

static void foreign_order_test_kernel(ForeignTestTally* tally, Arena* arena)
{
    ForeignTestEdge values_edge = { 0 };
    const uint64_t page = 4096;
    const uint64_t span = (1ull << 32) * 4;
    uint8_t* mapping = mmap(0, span + 2 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    bool mapped = mapping != MAP_FAILED;
    if (mapped)
    {
        values_edge = (ForeignTestEdge){ .mapping = mapping, .mapping_size = span + 2 * page, .end = mapping + page + span };
        mapped = mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0 &&
                 mprotect(mapping + page + (1ull << 31) * 4, page, PROT_READ | PROT_WRITE) == 0 &&
                 mprotect(mapping + span, page, PROT_READ | PROT_WRITE) == 0;
    }
    foreign_test_expect(tally, mapped, "reserve sparse value map for unsigned high-ID addressing", 0);
    if (mapped)
    {
        uint32_t* values = (uint32_t*)(mapping + page);
        values[0] = 11;
        values[0x80000000u] = 23;
        values[UINT32_MAX - 2] = 37;
        const uint64_t keys[] = { 0x1234567800000000ull, 0x1234567880000000ull, 0x12345678fffffffdull };
        uint32_t gathered[16];
        _mm512_storeu_si512(gathered, foreign_order_values(keys, BUSTER_ARRAY_LENGTH(keys), values));
        bool correct = gathered[0] == 11 && gathered[1] == 23 && gathered[2] == 37;
        for (uint32_t i = BUSTER_ARRAY_LENGTH(keys); i < BUSTER_ARRAY_LENGTH(gathered); i += 1)
        {
            correct = correct && gathered[i] == 0;
        }
        foreign_test_expect(tally, correct, "gather zero-extends IDs at bit 31 and UINT32_MAX, with inactive tails", 0);
    }
    foreign_test_edge_release(&values_edge);
    for (uint32_t test = 0; test < 69; test += 1)
    {
        const uint32_t count = test < 66 ? test : 4095 + test - 66;
        ForeignTestEdge order_edge = { 0 };
        ForeignTestEdge sort_edge = { 0 };
        ForeignTestEdge temporary_edge = { 0 };
        const bool guarded = foreign_test_edge(&order_edge, (uint64_t)count * 8) && foreign_test_edge(&sort_edge, (uint64_t)count * 8) &&
                             foreign_test_edge(&temporary_edge, (uint64_t)count * 8);
        foreign_test_expect(tally, guarded, "guard order and radix buffers immediately before PROT_NONE", 0);
        Scratch scratch = scratch_begin(arena);
        uint64_t* original = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count + 1);
        uint64_t* expected = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count + 1);
        for (uint32_t shape = 0; guarded && original != 0 && expected != 0 && shape < 3; shape += 1)
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                ForeignNames names = { .order = (uint64_t*)order_edge.end - count, .count = count };
                uint64_t* keys = (uint64_t*)sort_edge.end - count;
                uint64_t* temporary = (uint64_t*)temporary_edge.end - count;
                uint32_t any = 0;
                uint32_t every = UINT32_MAX;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    const uint32_t ordinal = shape == 0 ? i : shape == 1 ? UINT32_MAX - 2 - i : 0x80000000u + i * 65537u;
                    original[i] = ((uint64_t)(i % 5 == 0) << 32) | ((uint64_t)ordinal + 1);
                    names.order[i] = original[i];
                    keys[i] = ((uint64_t)ordinal << 32) | i;
                    expected[i] = keys[i];
                    any |= ordinal;
                    every &= ordinal;
                }
                for (uint32_t i = 1; i < count; i += 1)
                {
                    const uint64_t key = expected[i];
                    uint32_t at = i;
                    while (at != 0 && (expected[at - 1] >> 32) > (key >> 32))
                    {
                        expected[at] = expected[at - 1];
                        at -= 1;
                    }
                    expected[at] = key;
                }
                const ForeignPath mode = path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR;
                const uint64_t* sorted = foreign_order_sort(keys, temporary, count, mode, any ^ every);
                foreign_test_expect(tally, memcmp(sorted, expected, (uint64_t)count * 8) == 0, "full-width ordinal radix agrees with independent insertion oracle", 0);
                for (uint32_t i = 0; i < count; i += 1)
                {
                    expected[i] = i;
                }
                for (uint32_t i = 1; i < count; i += 1)
                {
                    const uint64_t id = expected[i];
                    uint32_t at = i;
                    while (at != 0 && original[expected[at - 1]] > original[id])
                    {
                        expected[at] = expected[at - 1];
                        at -= 1;
                    }
                    expected[at] = id;
                }
                bool ordered = foreign_name_order(&names, scratch.arena, mode);
                for (uint32_t i = 0; ordered && i < count; i += 1)
                {
                    ordered = (uint32_t)names.order[i] == expected[i] && (uint32_t)(names.order[i] >> 32) == (uint32_t)original[expected[i]] - 1;
                }
                foreign_test_expect(tally, ordered, "regular and library-only classes retain every high ordinal bit", 0);
            }
        }
        foreign_test_expect(tally, original != 0 && expected != 0, "allocate independent order oracles", 0);
        scratch_end(scratch);
        foreign_test_edge_release(&order_edge);
        foreign_test_edge_release(&sort_edge);
        foreign_test_edge_release(&temporary_edge);
    }
    for (uint32_t room = 0; room < 2; room += 1)
    {
        uint64_t original[] = { 4, 1, (1ull << 32) | 2, 3 };
        uint64_t order[BUSTER_ARRAY_LENGTH(original)];
        _Alignas(uint64_t) uint8_t storage[40];
        memcpy(order, original, sizeof(order));
        const Arena saved = thread_context.scratch[0];
        thread_context.scratch[0] = (Arena){ .base = storage, .capacity = room == 0 ? 8 : sizeof(storage) };
        ForeignNames names = { .order = order, .count = BUSTER_ARRAY_LENGTH(order) };
        const bool refused = !foreign_name_order(&names, arena, FOREIGN_PATH_VECTOR) && memcmp(order, original, sizeof(order)) == 0 && thread_context.scratch[0].length == 0;
        thread_context.scratch[0] = saved;
        foreign_test_expect(tally, refused, "each sort allocation refuses without publishing a partial order", 0);
    }
}

static bool foreign_order_test_compile(const char* directory, ForeignTestTally* tally)
{
    ForeignTestPath source;
    ForeignTestPath object;
    ForeignTestPath output;
    ForeignTestPath errors;
    bool result = true;
    for (uint32_t fixture = 0; fixture < 4; fixture += 1)
    {
        snprintf(source, sizeof(source), "%s/fo_%u.c", directory, fixture);
        snprintf(object, sizeof(object), "%s/fo_%u.%s", directory, fixture, fixture == 3 ? "so" : "o");
        snprintf(output, sizeof(output), "%s/fo_compile.out", directory);
        snprintf(errors, sizeof(errors), "%s/fo_compile.err", directory);
        FILE* file = fopen(source, "w");
        bool written = file != 0;
        if (file != 0)
        {
            if (fixture == 0)
            {
                written = fprintf(file, "int fo_number = 17; int fo_second = 90; int fo_function(void) { return 25; }\n") > 0;
                for (uint32_t i = 0; written && i < BUSTER_FOREIGN_ORDER_TEST_SYMBOLS; i += 1)
                {
                    written = fprintf(file, "int fo_data_%u = %u;\n", i, i) > 0;
                }
            }
            else if (fixture == 1)
            {
                written = fprintf(file, "int fo_archive(void) { return 7; } int fo_loaded = 13;\n") > 0;
            }
            else if (fixture == 2)
            {
                for (uint32_t i = 0; written && i < BUSTER_FOREIGN_ORDER_TEST_UNUSED; i += 1)
                {
                    written = fprintf(file, "int fo_unused_%u = %u;\n", i, i) > 0;
                }
            }
            else
            {
                written = fprintf(file, "extern int fo_number; extern int fo_second; extern int fo_library_only __attribute__((weak));\n"
                                        "int fo_library_data = 11; int fo_library(void) { return fo_number + fo_second - 82; }\n"
                                        "void* fo_library_pointer = &fo_library_only;\n"
                                        "int fo_copy_one = 23; int fo_copy_two = 31;\n"
                                        "extern int fo_copy_alias __attribute__((alias(\"fo_copy_one\")));\n") > 0;
            }
            written = fclose(file) == 0 && written;
        }
        char* arguments[] = {
            (char*)"cc", (char*)"-O1", (char*)"-fPIC", (char*)"-fno-asynchronous-unwind-tables", (char*)"-fno-unwind-tables",
            (char*)(fixture == 3 ? "-shared" : "-c"), (char*)"-nostdlib", source, (char*)"-o", object, 0,
        };
        const bool compiled = written && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, compiled, "compile original foreign-order C fixture", source);
        result = compiled && result;
    }
    for (uint32_t dynamic = 0; dynamic < 2; dynamic += 1)
    {
        snprintf(source, sizeof(source), "%s/fo_start_%u.s", directory, dynamic);
        snprintf(object, sizeof(object), "%s/fo_start_%u.o", directory, dynamic);
        FILE* file = fopen(source, "w");
        bool written = file != 0;
        if (file != 0)
        {
            written = fprintf(file, ".text\n.globl _start\n.type _start,@function\n_start:\n"
                                    "mov fo_number@GOTPCREL(%%rip),%%rax\ncmpl $17,(%%rax)\njne 1f\n"
                                    "mov fo_second@GOTPCREL(%%rip),%%rax\ncmpl $90,(%%rax)\njne 1f\n"
                                    "call fo_function\ncmp $25,%%eax\njne 1f\ncall fo_archive\ncmp $7,%%eax\njne 1f\n"
                                    "lea __start_fo_alpha(%%rip),%%rax\nlea __stop_fo_alpha(%%rip),%%rdx\nsub %%rax,%%rdx\ncmp $8,%%rdx\njne 1f\n"
                                    "cmpq $19,(%%rax)\njne 1f\nlea __start_fo_beta(%%rip),%%rax\nlea __stop_fo_beta(%%rip),%%rdx\n"
                                    "sub %%rax,%%rdx\ncmp $8,%%rdx\njne 1f\ncmpq $29,(%%rax)\njne 1f\n") > 0;
            if (dynamic != 0)
            {
                written = fprintf(file, "call fo_library@PLT\ncmp $25,%%eax\njne 1f\n"
                                        "mov fo_library_data@GOTPCREL(%%rip),%%rax\ncmpl $11,(%%rax)\njne 1f\n"
                                        "lea fo_copy_one(%%rip),%%rax\ncmpl $23,(%%rax)\njne 1f\n"
                                        "lea fo_copy_two(%%rip),%%rax\ncmpl $31,(%%rax)\njne 1f\n") > 0 && written;
            }
            written = fprintf(file, "mov $42,%%edi\njmp 2f\n1: mov $101,%%edi\n2: mov $60,%%eax\nsyscall\n"
                                    ".size _start,.-_start\n.section fo_alpha,\"aw\",@progbits\n.quad 19\n"
                                    ".section fo_beta,\"aw\",@progbits\n.quad 29\n.section .note.GNU-stack,\"\",@progbits\n") > 0 && written;
            written = fclose(file) == 0 && written;
        }
        char* arguments[] = { (char*)"cc", (char*)"-c", source, (char*)"-o", object, 0 };
        const bool compiled = written && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, compiled, "compile original foreign-order entry fixture", source);
        result = compiled && result;
    }
    return result;
}

// Independent oracle: walk ordinary object records in round/item order first, then library-only records. A repeated
// spelling contributes just its first record even when an earlier DSO mentioned a later object's definition.
static bool foreign_order_test_symbols(const Foreign* foreign, const ForeignLink* converted, Arena* arena)
{
    Scratch scratch = scratch_begin(arena);
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, (uint64_t)foreign->names.count + 1);
    bool result = seen != 0;
    SymbolId previous = 0;
    if (seen != 0)
    {
        memset(seen, 0, (uint64_t)foreign->names.count + 1);
        for (uint32_t library = 0; result && library < 2; library += 1)
        {
            for (uint32_t r = 0; result && r < foreign->round_count; r += 1)
            {
                const ForeignRound* round = &foreign->rounds[r];
                for (uint32_t o = round->first_object; result && o < round->end_object; o += 1)
                {
                    const ForeignObject* object = &foreign->objects[o];
                    for (uint32_t s = 0; result && (object->library != 0) == (library != 0) && s < object->global_count; s += 1)
                    {
                        const uint32_t name = round->symbols.name_id[object->global_begin - round->first_symbol + s];
                        const SymbolId symbol = converted->names[name];
                        const bool first = seen[name] == 0;
                        result = !first || symbol == 0 || symbol > previous;
                        previous = first && symbol != 0 ? symbol : previous;
                        seen[name] = 1;
                    }
                }
            }
        }
    }
    scratch_end(scratch);
    return result;
}

static bool foreign_order_test_section(String8 reference, String8 actual, bool dynamic, const char* name)
{
    const ExecutableTestImage first = executable_test_image_of(reference, dynamic ? ET_DYN : ET_EXEC);
    const ExecutableTestImage second = executable_test_image_of(actual, dynamic ? ET_DYN : ET_EXEC);
    const Elf64_Shdr* a = executable_test_section(&first, name);
    const Elf64_Shdr* b = executable_test_section(&second, name);
    const bool result = a != 0 && b != 0 && a->sh_size == b->sh_size && a->sh_offset <= reference.length && a->sh_size <= reference.length - a->sh_offset &&
                        b->sh_offset <= actual.length && b->sh_size <= actual.length - b->sh_offset &&
                        memcmp(reference.pointer + a->sh_offset, actual.pointer + b->sh_offset, a->sh_size) == 0;
    return result;
}

// The real command path: the expanded archive has the same loaded member and thousands of unused index entries.
// Its default partition count differs, without any testing override in the driver or production environment.
static void foreign_order_test_driver(const char* directory, Arena* arena, ForeignTestTally* tally, bool dynamic, String8 interpreter)
{
    const uint64_t saved = arena->length;
    String8 reference = { 0 };
    for (uint32_t archive = 0; archive < 2; archive += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
            {
                ForeignTestPath executable;
                ForeignTestPath output;
                ForeignTestPath errors;
                char lanes[16];
                snprintf(executable, sizeof(executable), "%s/fo_driver_%u_%u_%u_%u", directory, dynamic, archive, path, foreign_test_lanes[l]);
                snprintf(output, sizeof(output), "%s.out", executable);
                snprintf(errors, sizeof(errors), "%s.err", executable);
                snprintf(lanes, sizeof(lanes), "%u", foreign_test_lanes[l]);
                char* arguments[24] = {
                    (char*)"/proc/self/exe", (char*)"link-foreign", (char*)"--lanes", lanes, (char*)"-o", executable,
                };
                uint32_t count = 6;
                if (path == 0)
                {
                    arguments[count++] = (char*)"--scalar";
                }
                if (dynamic)
                {
                    arguments[count++] = (char*)"-pie";
                    arguments[count++] = (char*)"--dynamic-linker";
                    arguments[count++] = interpreter.pointer;
                    arguments[count++] = foreign_test_path(directory, "fo_3.so", arena);
                }
                arguments[count++] = foreign_test_path(directory, dynamic ? "fo_start_1.o" : "fo_start_0.o", arena);
                arguments[count++] = foreign_test_path(directory, "fo_0.o", arena);
                arguments[count++] = foreign_test_path(directory, archive == 0 ? "fo_small.a" : "fo_expanded.a", arena);
                arguments[count] = 0;
                const uint64_t iteration = arena->length;
                const bool linked = object_test_run(arguments, output, errors) == 0;
                char detail[96];
                snprintf(detail, sizeof(detail), "%s archive %u path %u lanes %u", dynamic ? "PIE" : "static", archive, path, foreign_test_lanes[l]);
                foreign_test_expect(tally, linked, "real driver publishes foreign-order fixture", detail);
                const String8 diagnostics = foreign_test_read(arena, errors);
                foreign_test_expect(tally, linked && diagnostics.pointer != 0 && diagnostics.length == 0, "real driver has no unexpected diagnostics", detail);
                foreign_test_expect(tally, linked && foreign_dynamic_test_run(directory, executable, 0, arena), "real driver output runs GOT/bounds/copy/archive checks", detail);
                const String8 bytes = linked ? foreign_test_read(arena, executable) : (String8){ 0 };
                if (reference.pointer != 0)
                {
                    foreign_test_expect(tally, bytes.pointer != 0 && reference.length == bytes.length && memcmp(reference.pointer, bytes.pointer, bytes.length) == 0,
                                        "real driver is byte-identical after unused-index and lane perturbations", detail);
                    foreign_test_expect(tally, foreign_order_test_section(reference, bytes, dynamic, ".symtab") && foreign_order_test_section(reference, bytes, dynamic, ".got"),
                                        "real driver preserves canonical symtab and GOT bytes", detail);
                    if (dynamic)
                    {
                        foreign_test_expect(tally, foreign_order_test_section(reference, bytes, true, ".dynsym"), "real driver preserves canonical dynsym bytes", detail);
                    }
                }
                arena_pop_to(arena, iteration);
                if (reference.pointer == 0 && linked)
                {
                    reference = foreign_test_read(arena, executable);
                    foreign_test_expect(tally, reference.pointer != 0, "retain original driver bytes", detail);
                }
            }
        }
    }
    arena_pop_to(arena, saved);
}

static void foreign_order_test_program(const char* directory, Arena* arena, ForeignTestTally* tally, bool dynamic, String8 interpreter)
{
    const uint64_t saved = arena->length;
    const char* small[] = { "fo_1.o" };
    const char* expanded[] = { "fo_2.o", "fo_1.o" };
    char* archives[] = {
        foreign_test_archive(directory, "fo_small.a", small, BUSTER_ARRAY_LENGTH(small), false, arena),
        foreign_test_archive(directory, "fo_expanded.a", expanded, BUSTER_ARRAY_LENGTH(expanded), false, arena),
    };
    String8 reference = { 0 };
    uint32_t original_id = UINT32_MAX;
    uint32_t original_partitions = 0;
    bool changed_id = false;
    bool changed_partitions = false;
    static const uint32_t targets[] = { 0, 1, 16, 100000 };
    for (uint32_t archive = 0; archive < BUSTER_ARRAY_LENGTH(archives); archive += 1)
    {
        char* arguments[] = {
            dynamic ? foreign_test_path(directory, "fo_3.so", arena) : foreign_test_path(directory, "fo_start_0.o", arena),
            dynamic ? foreign_test_path(directory, "fo_start_1.o", arena) : foreign_test_path(directory, "fo_0.o", arena),
            dynamic ? foreign_test_path(directory, "fo_0.o", arena) : archives[archive],
            dynamic ? archives[archive] : 0,
        };
        ForeignCommand command = { 0 };
        const bool ready = archives[archive] != 0 && foreign_test_command(&command, arguments, dynamic ? 4 : 3, arena);
        foreign_test_expect(tally, ready, "prepare foreign-order command", 0);
        for (uint32_t t = 0; ready && t < BUSTER_ARRAY_LENGTH(targets); t += 1)
        {
            command.partition_records = targets[t];
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
                {
                    const uint64_t iteration = arena->length;
                    Foreign foreign;
                    foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
                    Arena names = arena_reserve(1ull << 28);
                    ForeignLink converted = { 0 };
                    const ForeignLinkResult built = dynamic ? foreign_link_build_dynamic(&converted, &foreign, arena, &names, interpreter) :
                                                              foreign_link_build(&converted, &foreign, arena, &names);
                    const bool linked = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE;
                    char detail[192];
                    snprintf(detail, sizeof(detail), "%s archive %u target %u path %u lanes %u: resolve %u convert %u", dynamic ? "PIE" : "static", archive, targets[t], path,
                             foreign_test_lanes[l], (unsigned)foreign.failure.error, (unsigned)built.error);
                    foreign_test_expect(tally, linked, "convert foreign-order fixture", detail);
                    ForeignTestPath executable;
                    snprintf(executable, sizeof(executable), "%s/fo_output_%u_%u_%u_%u_%u", directory, dynamic, archive, t, path, foreign_test_lanes[l]);
                    bool emitted = false;
                    if (linked)
                    {
                        foreign_test_expect(tally, foreign_order_test_symbols(&foreign, &converted, arena), "globals follow first object record, then library-only records", detail);
                        const uint32_t id = foreign_test_name(&foreign, "fo_number");
                        original_id = original_id == UINT32_MAX ? id : original_id;
                        original_partitions = original_partitions == 0 ? foreign.partition_count : original_partitions;
                        changed_id = changed_id || original_id != id;
                        changed_partitions = changed_partitions || original_partitions != foreign.partition_count;
                        SymbolId* ids = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, converted.link.symbols.count);
                        ExecutableSymbols symbols = {
                            .ids = ids,
                            .count = ids != 0 ? foreign_driver_symbols_exact(&converted.link, (Range64){ .begin = 1, .end = converted.link.symbols.count }, ids) : 0,
                        };
                        ExecutableTestWrite write = {
                            .link = &converted.link,
                            .options = {
                                .path = { .pointer = executable, .length = strlen(executable) },
                                .entry = foreign_link_test_symbol(&foreign, &converted, "_start"),
                                .dynamic = dynamic ? &converted.dynamic : 0,
                                .symbols = &symbols,
                                .exact = path == 0,
                            },
                        };
                        emitted = ids != 0 && lane_run(foreign_test_lanes[l], executable_test_lane_main, &write, 0) && write.succeeded;
                        foreign_test_expect(tally, emitted, "write foreign-order executable", detail);
                        if (!emitted)
                        {
                            executable_test_print_failure(detail, &write.result);
                        }
                        foreign_test_expect(tally, emitted && foreign_dynamic_test_run(directory, executable, 0, arena), "execute foreign-order GOT/archive/DSO fixture", detail);
                        const String8 bytes = emitted ? foreign_test_read(arena, executable) : (String8){ 0 };
                        if (reference.pointer != 0)
                        {
                            foreign_test_expect(tally, bytes.pointer != 0 && reference.length == bytes.length && memcmp(reference.pointer, bytes.pointer, bytes.length) == 0,
                                                "partition and unused-index perturbations preserve every byte", detail);
                            foreign_test_expect(tally, foreign_order_test_section(reference, bytes, dynamic, ".symtab"), "symbol-table bytes retain canonical order", detail);
                            foreign_test_expect(tally, foreign_order_test_section(reference, bytes, dynamic, ".got"), "GOT bytes retain canonical slots", detail);
                            if (dynamic)
                            {
                                foreign_test_expect(tally, foreign_order_test_section(reference, bytes, true, ".dynsym"), "dynamic-symbol bytes retain canonical order", detail);
                            }
                        }
                    }
                    arena_release(&names);
                    foreign_release(&foreign);
                    arena_pop_to(arena, iteration);
                    if (reference.pointer == 0 && emitted)
                    {
                        reference = foreign_test_read(arena, executable);
                        foreign_test_expect(tally, reference.pointer != 0, "retain unperturbed executable", detail);
                    }
                }
            }
        }
    }
    foreign_test_expect(tally, changed_id && changed_partitions, "property matrix actually changes hash partitions and name IDs", 0);
    arena_pop_to(arena, saved);
}

static bool foreign_order_test(const char* directory, Arena* arena)
{
    ForeignTestTally tally = { .check = "foreign canonical name order" };
    foreign_order_test_kernel(&tally, arena);
    Runtime runtime;
    const bool loader = runtime_import(&runtime) == RUNTIME_ERROR_NONE && runtime.loader_path.length != 0;
    foreign_test_expect(&tally, loader, "find dynamic loader for PIE order comparison", 0);
    if (foreign_order_test_compile(directory, &tally))
    {
        foreign_order_test_program(directory, arena, &tally, false, (String8){ 0 });
        if (loader)
        {
            foreign_order_test_program(directory, arena, &tally, true, runtime.loader_path);
        }
        foreign_order_test_driver(directory, arena, &tally, false, (String8){ 0 });
        if (loader)
        {
            foreign_order_test_driver(directory, arena, &tally, true, runtime.loader_path);
        }
    }
    runtime_release(&runtime);
    return foreign_test_report(&tally);
}
