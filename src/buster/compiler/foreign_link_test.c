// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Foreign resolution's executable handoff (foreign_link.c). foreign_test.c includes this file after its helpers.
// Original assembly inputs test canonical globals, local and section symbols, common allocation, selected COMDAT
// sections and extracted archive members together, through a static executable that checks its relocated values.
// Every exact/vector resolution and lane count builds a fresh Link; their executable bytes must agree. GNU ld and
// lld link and run the same inputs with --cross-check. Conversion failures are checked before any output is written.

typedef struct ForeignLinkTestSource ForeignLinkTestSource;
struct ForeignLinkTestSource
{
    const char* name;
    const char* text;
};

#define BUSTER_FOREIGN_LINK_TEST_BATCH_IDS "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64"

static const ForeignLinkTestSource foreign_link_test_sources[] = {
    {
        .name = "fl_entry",
        .text =
            ".text\n.globl _start\n.type _start,@function\n_start:\n"
            "call winner\ncmp $11,%eax\njne .Lfail\n"
            "call chosen\ncmp $23,%eax\njne .Lfail\n"
            "call loser_live\ncmp $31,%eax\njne .Lfail\n"
            "call archived\ncmp $7,%eax\njne .Lfail\n"
            "mov local_value(%rip),%eax\ncmp $1,%eax\njne .Lfail\n"
            "mov local_pointer(%rip),%rdx\nlea local_value(%rip),%rcx\ncmp %rcx,%rdx\njne .Lfail\n"
            "mov section_pointer(%rip),%rdx\ncmpl $1,(%rdx)\njne .Lfail\n"
            "mov loser_local_pointer(%rip),%rdx\ncmpl $200,(%rdx)\njne .Lfail\n"
            "call *loser_pointer(%rip)\ncmp $23,%eax\njne .Lfail\n"
            "lea shared_common(%rip),%rdx\ntest $63,%edx\njne .Lfail\n"
            "cmpq $0,(%rdx)\njne .Lfail\ncmpq $0,56(%rdx)\njne .Lfail\n"
            "movq $17,56(%rdx)\ncmpq $17,56(%rdx)\njne .Lfail\n"
            "cmpq $64,common_size(%rip)\njne .Lfail\n"
            "cmpl $9,replaced_common(%rip)\njne .Lfail\n"
            "mov $magic_value,%eax\ncmp $5,%eax\njne .Lfail\n"
            "mov $magic_value,%rax\ncmp $5,%rax\njne .Lfail\n"
            "movabs $magic_value,%rax\ncmp $5,%rax\njne .Lfail\n"
            "movabs $absent_weak,%rax\ntest %rax,%rax\njne .Lfail\n"
            "cmpl $0,weak_tls_offset(%rip)\njne .Lfail\n"
            "mov winner@GOTPCREL(%rip),%rdx\nlea winner(%rip),%rcx\ncmp %rcx,%rdx\njne .Lfail\n"
            "lea batch_pointers(%rip),%rdx\nmov $65,%ecx\nxor %eax,%eax\nxor %r8d,%r8d\n"
            ".Lsum:\nmov (%rdx),%rsi\ncmp %r8,(%rsi)\njne .Lfail\ninc %r8\nadd (%rsi),%rax\nadd $8,%rdx\ndec %ecx\njnz .Lsum\n"
            "cmp $2080,%rax\njne .Lfail\nmov $42,%edi\njmp .Lexit\n"
            ".Lfail:\nmov $101,%edi\n.Lexit:\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
            ".weak absent_weak\n.weak absent_tls\n.type absent_tls,@tls_object\n.comm shared_common,64,8\n.comm replaced_common,8,8\n"
            ".section .rodata\n.long 99\n.type local_value,@object\nlocal_value:\n.long 1\n.size local_value,4\n"
            ".data\n.balign 8\nlocal_pointer:\n.quad local_value\nsection_pointer:\n.quad .rodata+4\n"
            "weak_tls_offset:\n.long absent_tls@TPOFF\n.balign 8\ncommon_size:\n.quad shared_common@SIZE\nbatch_pointers:\n"
            ".irp N," BUSTER_FOREIGN_LINK_TEST_BATCH_IDS "\n.quad foreign_batch_\\N\n.endr\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fl_weak",
        .text =
            ".text\n.weak winner\n.hidden winner\n.type winner,@function\nwinner:\nmov $99,%eax\nret\n.size winner,.-winner\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fl_first",
        .text =
            ".text\n.globl winner\n.type winner,@function\nwinner:\n.cfi_startproc\nmov $11,%eax\nret\n.cfi_endproc\n.size winner,.-winner\n"
            ".section .text.chosen,\"axG\",@progbits,chosen,comdat\n.weak chosen\n.type chosen,@function\n"
            "chosen:\n.cfi_startproc\nmov $23,%eax\nret\n.cfi_endproc\n.size chosen,.-chosen\n"
            ".comm shared_common,8,64\n.globl magic_value\n.set magic_value,5\n"
            ".data\n.globl replaced_common\n.type replaced_common,@object\nreplaced_common:\n.long 9\n.size replaced_common,4\n"
            ".balign 8\n.irp N," BUSTER_FOREIGN_LINK_TEST_BATCH_IDS "\n"
            ".globl foreign_batch_\\N\n.type foreign_batch_\\N,@object\nforeign_batch_\\N:\n.quad \\N\n.size foreign_batch_\\N,8\n.endr\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fl_loser",
        .text =
            ".section .text.chosen,\"axG\",@progbits,chosen,comdat\n.weak chosen\n.type chosen,@function\n"
            "chosen:\n.cfi_startproc\ncall discarded_only_missing\nmov $99,%eax\nret\n.cfi_endproc\n.size chosen,.-chosen\n"
            ".text\n.globl loser_live\n.type loser_live,@function\nloser_live:\n.cfi_startproc\nmov $31,%eax\nret\n.cfi_endproc\n.size loser_live,.-loser_live\n"
            ".section .rodata\n.type local_value,@object\nlocal_value:\n.long 200\n.size local_value,4\n"
            ".data\n.balign 8\n.globl loser_pointer\n.type loser_pointer,@object\nloser_pointer:\n.quad chosen\n.size loser_pointer,8\n"
            ".globl loser_local_pointer\n.type loser_local_pointer,@object\nloser_local_pointer:\n.quad local_value\n.size loser_local_pointer,8\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fl_archive",
        .text =
            ".text\n.globl archived\n.type archived,@function\narchived:\nmov $7,%eax\nret\n.size archived,.-archived\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fl_unused",
        .text =
            ".text\n.globl winner\n.type winner,@function\nwinner:\nmov $77,%eax\nret\n.size winner,.-winner\n"
            ".globl unused_archive\nunused_archive:\nret\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fl_undefined",
        .text =
            ".text\n.globl need_missing\n.type need_missing,@function\nneed_missing:\ncall no_such_foreign\nret\n.size need_missing,.-need_missing\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fl_tiny",
        .text = ".text\n.local tiny\n.type tiny,@function\ntiny:\nret\n.size tiny,.-tiny\n.section .note.GNU-stack,\"\",@progbits\n",
    },
};

static bool foreign_link_test_compile(const char* directory, ForeignTestTally* tally)
{
    bool result = true;
    ForeignTestPath source;
    ForeignTestPath object;
    ForeignTestPath output;
    ForeignTestPath errors;
    snprintf(output, sizeof(output), "%s/fl_compile.out", directory);
    snprintf(errors, sizeof(errors), "%s/fl_compile.err", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(foreign_link_test_sources); i += 1)
    {
        const ForeignLinkTestSource* fixture = &foreign_link_test_sources[i];
        snprintf(source, sizeof(source), "%s/%s.s", directory, fixture->name);
        snprintf(object, sizeof(object), "%s/%s.o", directory, fixture->name);
        char* arguments[] = {
            (char*)"cc",
            (char*)"-c",
            (char*)"-Wa,-L",
            source,
            (char*)"-o",
            object,
            0,
        };
        const bool written = foreign_test_write(source, fixture->text, strlen(fixture->text));
        const bool compiled = written && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, compiled, "compile handoff fixture", fixture->name);
        result = compiled && result;
    }
    return result;
}

static SymbolId foreign_link_test_symbol(const Foreign* foreign, const ForeignLink* converted, const char* text)
{
    const uint32_t name = foreign_test_name(foreign, text);
    return name != 0 ? converted->names[name - 1] : 0;
}

static void foreign_link_test_structure(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* converted)
{
    const Link* link = &converted->link;
    const SymbolId common = foreign_link_test_symbol(foreign, converted, "shared_common");
    const SymbolId winner = foreign_link_test_symbol(foreign, converted, "winner");
    const SymbolId absent = foreign_link_test_symbol(foreign, converted, "absent_weak");
    const SymbolId absent_tls = foreign_link_test_symbol(foreign, converted, "absent_tls");
    const SymbolId absolute = foreign_link_test_symbol(foreign, converted, "magic_value");
    const SymbolId replaced = foreign_link_test_symbol(foreign, converted, "replaced_common");
    foreign_test_expect(tally, foreign->object_count == 6, "only selected archive member converted", 0);
    foreign_test_expect(tally, foreign_test_name(foreign, "unused_archive") == 0, "unselected archive name absent", 0);
    foreign_test_expect(tally, foreign_test_name(foreign, "discarded_only_missing") != 0 && foreign_link_test_symbol(foreign, converted, "discarded_only_missing") == 0,
                        "undefined name referenced only by discarded COMDAT is omitted", 0);
    foreign_test_expect(tally, common != 0 && link->symbols.size[common] == 64, "common size merged", 0);
    const AtomId common_atom = common != 0 ? link->symbols.atom[common] : 0;
    foreign_test_expect(tally, common_atom != 0 && link->atoms.class[common_atom] == BUSTER_LINK_CLASS_BSS && link->atoms.alignment_log2[common_atom] == 6,
                        "common allocated with merged alignment", 0);
    foreign_test_expect(tally, winner != 0 && (link->symbols.flags[winner] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL &&
                                  (link->symbols.flags[winner] & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_HIDDEN,
                        "strong winner retains merged visibility", 0);
    foreign_test_expect(tally, absent != 0 && link->symbols.source[absent] == BUSTER_LINK_SOURCE_NONE &&
                                  (link->symbols.flags[absent] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK,
                        "unresolved weak remains weak", 0);
    foreign_test_expect(tally, absent_tls != 0 && link->symbols.source[absent_tls] == BUSTER_LINK_SOURCE_NONE &&
                                  (link->symbols.flags[absent_tls] & (BUSTER_LINK_SYMBOL_TYPE | BUSTER_LINK_SYMBOL_BINDING)) == (BUSTER_LINK_SYMBOL_TLS | BUSTER_LINK_SYMBOL_WEAK),
                        "unresolved weak retains TLS type", 0);
    foreign_test_expect(tally, absolute != 0 && link->symbols.source[absolute] == BUSTER_LINK_SOURCE_ABSOLUTE && link->symbols.value[absolute] == 5,
                        "absolute definition retained", 0);
    foreign_test_expect(tally, replaced != 0 && link->symbols.size[replaced] == 4 && link->symbols.atom[replaced] != 0 &&
                                  link->atoms.class[link->symbols.atom[replaced]] == BUSTER_LINK_CLASS_DATA,
                        "strong data replaces common", 0);
    uint32_t discarded = 0;
    uint32_t frames = 0;
    uint32_t pruned = 0;
    for (uint32_t id = 1; id < foreign->object_count; id += 1)
    {
        const ForeignObject* object = &foreign->objects[id];
        const ImportObject* imported = &converted->objects[id];
        foreign_test_expect(tally, imported->section_count == object->elf.section_count && imported->symbol_count == object->symbols.count,
                            "import maps retain input index space", 0);
        for (uint32_t i = 0; i < object->global_count; i += 1)
        {
            uint32_t at = 0;
            const ForeignSymbols* symbols = foreign_test_symbols(foreign, object->global_begin + i, &at);
            const uint32_t name = symbols->name_id[at];
            const uint32_t input_symbol = object->first_global + i;
            const bool canonical = name < foreign->names.count && input_symbol < imported->symbol_count && imported->symbols[input_symbol] == converted->names[name];
            foreign_test_expect(tally, canonical, "every global maps to its canonical winner", 0);
        }
        for (uint32_t section = 1; section < object->elf.section_count; section += 1)
        {
            if (object->discarded != 0 && object->discarded[section] != 0)
            {
                discarded += 1;
                foreign_test_expect(tally, section < imported->section_count && imported->section_atoms[section] == 0, "discarded COMDAT has no output atom", 0);
            }
            const AtomId atom = imported->section_atoms[section];
            if (atom != 0 && link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
            {
                frames += executable_eh_frame_walk(link, atom, 0, 0, 0);
                const ForeignLinkUnwind* frame = foreign_link_unwind(converted, id, section);
                if (frame != 0 && frame->plan.bytes.length < frame->plan.old_length)
                {
                    pruned += 1;
                    const Elf64_Shdr original = import_section_header(&object->elf, section);
                    foreign_test_expect(tally, original.sh_size == frame->plan.old_length && frame->plan.fde_count == 1,
                                        "COMDAT pruning keeps the surviving FDE and original input size", 0);
                }
            }
        }
    }
    foreign_test_expect(tally, discarded != 0, "fixture contains a discarded COMDAT section", 0);
    foreign_test_expect(tally, pruned == 1 && frames == 3, "discarded COMDAT loses only its matching FDE", 0);
}

static void foreign_link_test_oracles(ForeignTestTally* tally, const char* directory, char** inputs, uint32_t count)
{
    static const char* const linkers[] = {
        "ld.bfd",
        "ld.lld",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(linkers); i += 1)
    {
        ForeignTestPath executable;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(executable, sizeof(executable), "%s/fl_reference_%u", directory, i);
        snprintf(output, sizeof(output), "%s/fl_reference_%u.out", directory, i);
        snprintf(errors, sizeof(errors), "%s/fl_reference_%u.err", directory, i);
        char* arguments[16] = {
            (char*)linkers[i],
            (char*)"-static",
            (char*)"-e",
            (char*)"_start",
            (char*)"-o",
            executable,
        };
        for (uint32_t j = 0; j < count; j += 1)
        {
            arguments[6 + j] = inputs[j];
        }
        const bool linked = object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, linked, "reference links handoff fixture", linkers[i]);
        char* run[] = {
            executable,
            0,
        };
        foreign_test_expect(tally, linked && object_test_run(run, output, errors) == 42, "reference executes handoff fixture", linkers[i]);
    }
}

static void foreign_link_test_program(ForeignTestTally* tally, const char* directory, Arena* arena, bool cross_check)
{
    const uint64_t saved = arena->length;
    const char* members[] = {
        "fl_archive.o",
        "fl_unused.o",
    };
    char* inputs[] = {
        foreign_test_path(directory, "fl_entry.o", arena),
        foreign_test_path(directory, "fl_weak.o", arena),
        foreign_test_path(directory, "fl_first.o", arena),
        foreign_test_path(directory, "fl_loser.o", arena),
        foreign_test_archive(directory, "fl_members.a", members, BUSTER_ARRAY_LENGTH(members), false, arena),
    };
    bool ready = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(inputs); i += 1)
    {
        ready = ready && inputs[i] != 0;
    }
    ForeignCommand command = { 0 };
    ready = ready && foreign_test_command(&command, inputs, BUSTER_ARRAY_LENGTH(inputs), arena);
    command.partition_records = BUSTER_FOREIGN_TEST_PARTITION_RECORDS;
    foreign_test_expect(tally, ready, "prepare handoff command and archive", 0);
    ForeignTestPath reference_path;
    ForeignTestPath executable;
    ForeignTestPath output;
    ForeignTestPath errors;
    snprintf(reference_path, sizeof(reference_path), "%s/fl_exact", directory);
    snprintf(output, sizeof(output), "%s/fl_run.out", directory);
    snprintf(errors, sizeof(errors), "%s/fl_run.err", directory);
    String8 reference = { 0 };
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
        {
            Foreign foreign;
            foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
            foreign_test_expect(tally, foreign.failure.error == FOREIGN_ERROR_NONE, "resolve handoff fixture", 0);
            const uint64_t iteration = arena->length;
            Arena names = arena_reserve(1ull << 24);
            ForeignLink converted = { 0 };
            const ForeignLinkResult built = foreign_link_build(&converted, &foreign, arena, &names);
            char detail[128];
            snprintf(detail, sizeof(detail), "%s on %u lanes: error %u, object %u, name %u, import %u", path == 0 ? "exact" : "vector", foreign_test_lanes[l],
                     (unsigned)built.error, built.object, built.name, (unsigned)built.import.error);
            const bool linked = built.error == FOREIGN_LINK_ERROR_NONE;
            foreign_test_expect(tally, linked, "convert handoff fixture", detail);
            if (linked)
            {
                foreign_link_test_structure(tally, &foreign, &converted);
                const SymbolId entry = foreign_link_test_symbol(&foreign, &converted, "_start");
                const bool first = path == 0 && l == 0;
                // A different path per variant prevents a missing write from inheriting correct bytes from its twin.
                snprintf(executable, sizeof(executable), "%s/fl_variant_%u_%u", directory, path, foreign_test_lanes[l]);
                char* destination = first ? reference_path : executable;
                const String8 destination_name = {
                    .pointer = destination,
                    .length = strlen(destination),
                };
                ExecutableResult written = { 0 };
                const bool emitted = path == 0 ? executable_test_write_exact(&converted.link, entry, destination_name, foreign_test_lanes[l], &written) :
                                                executable_test_write(&converted.link, entry, destination_name, foreign_test_lanes[l], &written, 0, 0);
                foreign_test_expect(tally, emitted, "write converted executable", detail);
                char* run[] = {
                    destination,
                    0,
                };
                foreign_test_expect(tally, emitted && object_test_run(run, output, errors) == 42, "execute converted relocations and winners", detail);
                String8 bytes = emitted ? foreign_test_read(arena, destination) : (String8){ 0 };
                const char* unwind = emitted && bytes.pointer != 0 ? executable_test_unwind(&converted.link, bytes, BUSTER_EXECUTABLE_BASE, foreign.object_count, &written) : "no output";
                foreign_test_expect(tally, unwind == 0, "independent walk and lookup validate retained unwind ranges", unwind);
                if (!first)
                {
                    const bool same = reference.pointer != 0 && bytes.pointer != 0 && reference.length == bytes.length && memcmp(reference.pointer, bytes.pointer, bytes.length) == 0;
                    foreign_test_expect(tally, same, "exact/vector lane variants emit identical bytes", detail);
                }
            }
            arena_release(&names);
            foreign_release(&foreign);
            arena_pop_to(arena, iteration);
            if (path == 0 && l == 0)
            {
                reference = foreign_test_read(arena, reference_path);
                foreign_test_expect(tally, reference.pointer != 0, "retain exact executable bytes", 0);
            }
        }
    }
    if (ready && cross_check)
    {
        foreign_link_test_oracles(tally, directory, inputs, BUSTER_ARRAY_LENGTH(inputs));
    }
    arena_pop_to(arena, saved);
}

static void foreign_link_test_failures(ForeignTestTally* tally, const char* directory, Arena* arena)
{
    const uint64_t saved = arena->length;
    char* duplicate[] = {
        foreign_test_path(directory, "fl_first.o", arena),
        foreign_test_path(directory, "fl_unused.o", arena),
    };
    char* undefined[] = {
        foreign_test_path(directory, "fl_undefined.o", arena),
    };
    char* tiny[] = {
        foreign_test_path(directory, "fl_tiny.o", arena),
    };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        for (uint32_t fixture = 0; fixture < 3; fixture += 1)
        {
            const uint64_t iteration = arena->length;
            char** arguments = fixture == 0 ? duplicate : fixture == 1 ? undefined : tiny;
            const uint32_t count = fixture == 0 ? 2 : 1;
            ForeignCommand command = { 0 };
            bool ready = true;
            for (uint32_t i = 0; i < count; i += 1)
            {
                ready = ready && arguments[i] != 0;
            }
            ready = ready && foreign_test_command(&command, arguments, count, arena);
            foreign_test_expect(tally, ready, "prepare handoff refusal or tiny input", 0);
            if (ready)
            {
                Foreign foreign;
                foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, 1, 0);
                Arena names = arena_reserve(1ull << 24);
                ForeignLink converted = { 0 };
                const ForeignLinkResult built = foreign_link_build(&converted, &foreign, arena, &names);
                const ForeignLinkError expected = fixture == 0 ? FOREIGN_LINK_ERROR_DUPLICATE : fixture == 1 ? FOREIGN_LINK_ERROR_UNDEFINED : FOREIGN_LINK_ERROR_NONE;
                foreign_test_expect(tally, foreign.failure.error == FOREIGN_ERROR_NONE && built.error == expected, "conversion diagnostic agrees with fixture", 0);
                if (fixture < 2)
                {
                    const uint32_t name = foreign_test_name(&foreign, fixture == 0 ? "winner" : "no_such_foreign");
                    foreign_test_expect(tally, name != 0 && built.name == name, "conversion diagnostic names the failed symbol", 0);
                }
                else if (built.error == FOREIGN_LINK_ERROR_NONE)
                {
                    foreign_test_expect(tally, foreign.names.count == 0 && link_check(&converted.link) == LINK_ERROR_NONE, "tiny input without globals converts", 0);
                    Arena small = arena_reserve(4096);
                    const bool filled = small.base != 0 && arena_push(&small, small.capacity, 1) != 0;
                    foreign_test_expect(tally, filled, "reserve exhausted conversion arena", 0);
                    if (filled)
                    {
                        ForeignLink failed = { 0 };
                        const ForeignLinkResult memory = foreign_link_build(&failed, &foreign, &small, &names);
                        foreign_test_expect(tally, memory.error == FOREIGN_LINK_ERROR_MEMORY || memory.error == FOREIGN_LINK_ERROR_CAPACITY,
                                            "exhausted conversion arena refuses input", 0);
                    }
                    arena_release(&small);
                }
                arena_release(&names);
                foreign_release(&foreign);
            }
            arena_pop_to(arena, iteration);
        }
    }
    Foreign invalid = { 0 };
    invalid.failure.error = FOREIGN_ERROR_MALFORMED;
    ForeignLink converted = { 0 };
    const ForeignLinkResult rejected = foreign_link_build(&converted, &invalid, arena, arena);
    foreign_test_expect(tally, rejected.error == FOREIGN_LINK_ERROR_INPUT, "failed resolution refuses conversion", 0);
    arena_pop_to(arena, saved);
}

static int foreign_link_test_compare_time(const void* a, const void* b)
{
    const uint64_t left = *(const uint64_t*)a;
    const uint64_t right = *(const uint64_t*)b;
    return (left > right) - (left < right);
}

static double foreign_link_test_median(uint64_t* values, uint32_t count)
{
    qsort(values, count, sizeof(*values), foreign_link_test_compare_time);
    return count != 0 ? (double)values[count / 2] / 1000.0 : 0;
}

// Generated objects are frozen before the first iteration. runs == 0 checks both paths on every test lane count;
// otherwise one warm-up precedes `runs` measured iterations on one lane. Timing includes each stage's thread setup,
// and ends when the output is closed; execution, byte comparison and release are outside it. Arena bytes are retained
// producer/handoff storage, excluding mapped inputs and transient scratch; they are not peak resident memory.
static bool foreign_link_test_generated(const char* directory, Arena* arena, const uint32_t* sizes, uint32_t size_count, uint32_t runs)
{
    ForeignTestTally tally = {
        .check = runs != 0 ? "foreign output benchmark validation" : "foreign generated output",
    };
    for (uint32_t p = 0; p < size_count; p += 1)
    {
        const uint64_t saved = arena->length;
        LinkProgramOptions options = link_program_default_options();
        options.seed = 10001 + p;
        options.functions = sizes[p];
        options.units = (sizes[p] + 199) / 200;
        options.globals = 8;
        options.thread_locals = 2;
        options.filler = sizes[p] > 2000 ? 64 : 8;
        LinkProgram program;
        const bool generated = link_program_generate(&program, options);
        ForeignTestPath* paths = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestPath, options.units);
        char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, options.units);
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "fl_generated_%u", sizes[p]);
        bool ready = generated && paths != 0 && arguments != 0 && foreign_test_write_program(&program.link, options.units, directory, prefix, paths, arena);
        uint64_t input_bytes = 0;
        for (uint32_t u = 0; ready && u < options.units; u += 1)
        {
            arguments[u] = paths[u];
            struct stat status;
            ready = stat(paths[u], &status) == 0 && status.st_size >= 0;
            input_bytes += ready ? (uint64_t)status.st_size : 0;
        }
        ForeignCommand command = { 0 };
        ready = ready && foreign_test_command(&command, arguments, options.units, arena);
        foreign_test_expect(&tally, ready, "generate frozen output workload", prefix);
        const uint32_t lane_count = runs == 0 ? BUSTER_ARRAY_LENGTH(foreign_test_lanes) : 1;
        uint64_t* times = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)4 * runs);
        ready = ready && (runs == 0 || times != 0);
        foreign_test_expect(&tally, runs == 0 || times != 0, "reserve output timing samples", prefix);
        String8 reference = { 0 };
        for (uint32_t path = 0; ready && path < 2; path += 1)
        {
            for (uint32_t l = 0; l < lane_count; l += 1)
            {
                uint64_t retained = 0;
                uint64_t output_bytes = 0;
                bool valid = true;
                for (uint32_t run = 0; valid && run <= runs; run += 1)
                {
                    const uint64_t iteration = arena->length;
                    Arena names = arena_reserve(1ull << 30);
                    ForeignTestPath executable;
                    snprintf(executable, sizeof(executable), "%s/%s_%u_%u", directory, prefix, path, foreign_test_lanes[l]);
                    // Every measured output is new; validation never inherits bytes from a previous write.
                    const bool removed = unlink(executable) == 0 || errno == ENOENT;
                    const String8 destination = {
                        .pointer = executable,
                        .length = strlen(executable),
                    };
                    Foreign foreign;
                    const uint64_t start = foreign_now();
                    foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
                    const uint64_t resolved = foreign_now();
                    ForeignLink converted = { 0 };
                    const ForeignLinkResult built = foreign_link_build(&converted, &foreign, arena, &names);
                    const uint64_t handed_off = foreign_now();
                    const SymbolId entry = built.error == FOREIGN_LINK_ERROR_NONE ? foreign_link_test_symbol(&foreign, &converted, "_start") : 0;
                    ExecutableResult written = { 0 };
                    bool emitted = false;
                    if (removed && names.base != 0 && built.error == FOREIGN_LINK_ERROR_NONE)
                    {
                        emitted = path == 0 ? executable_test_write_exact(&converted.link, entry, destination, foreign_test_lanes[l], &written) :
                                              executable_test_write(&converted.link, entry, destination, foreign_test_lanes[l], &written, 0, 0);
                    }
                    const uint64_t finished = foreign_now();
                    valid = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE && emitted;
                    char detail[192];
                    snprintf(detail, sizeof(detail), "%u functions, %s, %u lanes, iteration %u: resolve %u convert %u object %u name %u import %u write %u",
                             sizes[p], path == 0 ? "exact" : "vector", foreign_test_lanes[l], run, (unsigned)foreign.failure.error, (unsigned)built.error,
                             built.object, built.name, (unsigned)built.import.error, (unsigned)written.error);
                    foreign_test_expect(&tally, valid, "generated producer, handoff and output succeed", detail);
                    if (valid)
                    {
                        retained = arena->length - iteration + names.length + foreign.arena.length;
                        for (uint32_t lane = 0; lane < foreign.lanes; lane += 1)
                        {
                            retained += foreign.lane_states[lane].arena.length;
                        }
                        output_bytes = written.file_size;
                        if (run != 0)
                        {
                            times[run - 1] = resolved - start;
                            times[runs + run - 1] = handed_off - resolved;
                            times[2 * runs + run - 1] = finished - handed_off;
                            times[3 * runs + run - 1] = finished - start;
                        }
                        valid = executable_test_runs(executable, program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES);
                        foreign_test_expect(&tally, valid, "generated output matches independent program model", detail);
                        const String8 bytes = foreign_test_read(arena, executable);
                        const bool first = path == 0 && l == 0 && run == 0;
                        const bool same = first ? bytes.pointer != 0 : bytes.pointer != 0 && reference.pointer != 0 && bytes.length == reference.length &&
                                                                      memcmp(bytes.pointer, reference.pointer, bytes.length) == 0;
                        foreign_test_expect(&tally, same, "generated executable bytes agree across paths and lanes", detail);
                        valid = valid && same;
                    }
                    arena_release(&names);
                    foreign_release(&foreign);
                    arena_pop_to(arena, iteration);
                    if (path == 0 && l == 0 && run == 0 && valid)
                    {
                        reference = foreign_test_read(arena, executable);
                        valid = reference.pointer != 0;
                        foreign_test_expect(&tally, valid, "retain generated exact executable", detail);
                    }
                }
                if (valid && runs != 0)
                {
                    const double resolve = foreign_link_test_median(times, runs);
                    const double convert = foreign_link_test_median(times + runs, runs);
                    const double write = foreign_link_test_median(times + 2 * runs, runs);
                    const double total = foreign_link_test_median(times + 3 * runs, runs);
                    printf("foreign-output functions %u units %u %s runs %u: resolve %.1f us convert %.1f us write %.1f us total %.1f us input %llu bytes output %llu bytes retained-arena %llu bytes throughput %.1f MiB/s\n",
                           sizes[p], options.units, path == 0 ? "exact" : "vector", runs, resolve, convert, write, total, (unsigned long long)input_bytes,
                           (unsigned long long)output_bytes, (unsigned long long)retained, total != 0 ? (double)input_bytes * 1000000.0 / (total * 1048576.0) : 0);
                }
            }
        }
        link_program_release(&program);
        arena_pop_to(arena, saved);
    }
    return foreign_test_report(&tally);
}

static bool foreign_link_test(const char* directory, Arena* arena, bool cross_check)
{
    ForeignTestTally tally = {
        .check = "foreign executable handoff",
    };
    if (foreign_link_test_compile(directory, &tally))
    {
        foreign_link_test_program(&tally, directory, arena, cross_check);
        foreign_link_test_failures(&tally, directory, arena);
    }
    const uint32_t sizes[] = {
        1,
        2000,
    };
    const bool generated = foreign_link_test_generated(directory, arena, sizes, BUSTER_ARRAY_LENGTH(sizes), 0);
    return foreign_test_report(&tally) && generated;
}

// One-lane producer/handoff/writer measurements. Pin externally to CPU 2 under the shared 9700X lock.
static int run_foreign_link_benchmark(int argc, char** argv)
{
    uint32_t sizes[16] = {
        1,
        2000,
        102400,
    };
    uint32_t size_count = 3;
    uint32_t runs = 21;
    const char* base = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    bool keep = false;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool value = i + 1 < argc;
        if (strcmp(argv[i], "--functions") == 0 && value)
        {
            const char* at = argv[i + 1];
            size_count = 0;
            while (valid && *at != 0)
            {
                char* end = 0;
                const uint64_t count = strtoull(at, &end, 10);
                valid = end != at && count > 0 && count <= BUSTER_LINK_PROGRAM_MAX_COUNT && size_count < BUSTER_ARRAY_LENGTH(sizes) && (*end == 0 || *end == ',');
                if (valid)
                {
                    sizes[size_count] = (uint32_t)count;
                    size_count += 1;
                    at = *end == ',' ? end + 1 : end;
                    valid = *end != ',' || *at != 0;
                }
            }
            valid = valid && size_count != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--runs") == 0 && value)
        {
            char* end = 0;
            const uint64_t count = strtoull(argv[i + 1], &end, 10);
            valid = end != argv[i + 1] && *end == 0 && count > 0 && count <= 1024;
            runs = valid ? (uint32_t)count : runs;
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && value)
        {
            base = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    ForeignTestPath directory;
    snprintf(directory, sizeof(directory), "%s/buster-foreign-output-XXXXXX", base);
    Arena arena = arena_reserve(BUSTER_FOREIGN_TEST_ARENA_BYTES);
    bool passed = false;
    if (!valid)
    {
        printf("usage: benchmark-foreign-output [--functions LIST] [--runs N] [--directory PATH] [--keep]\n");
    }
    else if (arena.base == 0 || mkdtemp(directory) == 0)
    {
        printf("benchmark-foreign-output: no room for the arena or output directory\n");
    }
    else
    {
        passed = foreign_link_test_generated(directory, &arena, sizes, size_count, runs);
        if (!keep && passed)
        {
            char* remove[] = {
                (char*)"rm",
                (char*)"-rf",
                directory,
                0,
            };
            object_test_run(remove, "/dev/null", "/dev/null");
        }
        else
        {
            printf("benchmark-foreign-output: kept %s\n", directory);
        }
    }
    arena_release(&arena);
    return passed ? 0 : 1;
}
