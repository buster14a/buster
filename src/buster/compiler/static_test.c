// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The static executables' tests with the C runtime (executable.c's static mode with ExecutableOptions.runtime, and
// dynamic.c's dynamic_static_inputs), for the test-static command:
//
//   build/compiler test-static [--seed N] [--functions N] [--directory PATH] [--cross-check] [--keep]
//
// main.c includes this file after dynamic_test.c, whose writes it makes (DynamicTestWrite and dynamic_test_lane_main),
// bind_test.c, whose bindings against the static runtime it writes (bind_test_program, bind_test_static_import and
// bind_test_static_sources), and executable_test.c, whose reading of an executable and of its unwind tables it shares,
// and calls run_static_tests for the command. Executables and their sources are written into a directory of their own
// under --directory (TMPDIR, or /tmp, by default), which the tests remove unless --keep says to leave it.
//
// Each program is bound against the system's static runtime (runtime_import_static) by bind.c and written by
// executable_write as a static executable with groups of 1, 2, 3 and 8 lanes, each time from zeroed fields over the
// last file scribbled over (executable_test_reset) and every other time over poisoned scratch memory, then by the
// writer's exact twins with 1 and 8 lanes: the files must be byte-identical. Each file is read back and checked by the
// test's own rules (static_test_structure):
//   - the headers: ET_EXEC from _start, no PT_INTERP or PT_DYNAMIC, a PT_NOTE with crt1.o's ABI note, and PT_TLS over
//     .tdata and .tbss;
//   - the IFUNCs: an IRELATIVE relocation in .rela.iplt for each stub's slot and for each IFUNC's GOT slot without a
//     stub, its addend the IFUNC's resolver; each stub in .iplt a jmp through its own slot; an IFUNC with a stub at its
//     stub's address for its calls, and everywhere when a fixup other than a call takes it, its GOT slot too, where
//     one whose stub only calls reach reads the stub's slot through the GOT, as lld's reads its .got.plt entry; and
//     __rela_iplt_start and __rela_iplt_end at .rela.iplt's bounds;
//   - every fixup's field (static_test_expected): test-executable's kinds' values, with an IFUNC's stub for its address,
//     a general- or local-dynamic access as the local-exec sequence it relaxes to, DTPOFF32 as an offset from the
//     thread pointer, an absolute symbol's value, nothing for a call of __tls_get_addr, which the access consumed, and
//     a load or call through the GOT that relaxes (executable_test_relaxes) as its rewritten instruction; and every
//     name of the linker's at the place its section or segment says;
//   - the init and fini arrays: their prioritized atoms first, by priority and then in id order, then the others in id
//     order, one after another from their section's start to its end;
//   - the unwind tables, by test-executable's rules (executable_test_unwind).
// The checks, in this order:
//   - Programs: test-link's sets of options that call libc (link_test_program_options), and one of --functions workers
//     (2,000 by default): each must run and write LinkProgram.output.
//   - Compiled: test-bind's C programs (bind_test_static_sources) and this file's (static_test_sources: libgcc's decimal
//     floating point, whose members reach their thread-local state by general-dynamic accesses, from a thread too;
//     GCC 14's heap trampolines, whose member's accesses are local-dynamic, where cc is GCC 14 or later; prioritized
//     constructors and destructors beside libgcc's cpuinfo.o's; and assembly with each general- and local-dynamic
//     sequence through the PLT and the GOT, with a local IFUNC called, pointed to and compared, and a global one
//     loaded through the GOT, with a weak IFUNC defined nowhere, and with absolute symbols that another object defines,
//     combined with ld -r), compiled by cc: each must run as cc -static's link of the same object does, with the same
//     status and output, both from directories of the same length (argv[0]).
//   - Refusals: assembly with a call of __tls_get_addr outside an access (refused naming the symbol), a general-dynamic
//     access without its prefixes (its fixup), a pointer to an absolute symbol farther than int32 from the image's
//     base (the symbol) and a lea of one whose offset from the base fits int32 but not its distance from the lea (its
//     fixup), each refused alike by every lane count and by the exact twins, without an output; and a lane short of
//     scratch memory (lane 0 of 1 and of 8 lanes, the last of 2 and of 8), with which every write that fails must fail
//     with EXECUTABLE_ERROR_MEMORY and no output, and one with room enough must run.
// With --cross-check, which needs cc with lld, each compiled program is linked by cc -static -fuse-ld=lld too, which
// must run alike and have as many IRELATIVE relocations, the same TLS segment's size and alignment, and init and fini
// arrays of the same sizes as ours; but lld need only link the one it links wrong (StaticTestSource.lld_wrong).
// The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, writing, the structure, the programs, the compiled programs, the refusals, and
// run_static_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>

// OS-dependent includes
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static const uint32_t static_test_lane_counts[] = { 1, 2, 3, 8 };

// ---------------------------------------------------------------------------------------------------------------------
// The tally

typedef struct StaticTestTally StaticTestTally;
struct StaticTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
};

static void static_test_expect(StaticTestTally* tally, bool condition, const char* what, uint64_t index)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < 10)
        {
            printf("  %s: %s (%llu)\n", tally->check, what, (unsigned long long)index);
        }
        tally->failures += 1;
    }
}

static bool static_test_report(const StaticTestTally* tally)
{
    printf("%s: %llu checks, %llu failed\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    return tally->failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The kinds
//
// Each link is written as the kind its runtime is for: a static executable (RUNTIME_KIND_STATIC) at
// BUSTER_EXECUTABLE_BASE, or a static PIE (RUNTIME_KIND_STATIC_PIE) at 0, whose cc links take -static-pie.

static bool static_test_pie(const Runtime* runtime)
{
    return runtime->kind == RUNTIME_KIND_STATIC_PIE;
}

static uint64_t static_test_base(bool pie)
{
    return pie ? 0 : BUSTER_EXECUTABLE_BASE;
}

// ---------------------------------------------------------------------------------------------------------------------
// Writing

// Writes a link bound against the static runtime to `path` as a static executable with a group of `lanes` lanes, from
// _start, with dynamic_static_inputs's inputs, or against the static PIE's runtime as a static PIE, with
// dynamic_static_pie_inputs's: whether executable_write succeeded, and its result. `write` holds the poison and starve
// options and whether the writer's exact twins write it.
static bool static_test_write_with(DynamicTestWrite* write, Link* link, const Bind* bind, const Runtime* runtime, String8 path, uint32_t lanes, ExecutableResult* result)
{
    ExecutableRuntime inputs;
    ExecutableDynamic dynamic;
    const bool pie = runtime->kind == RUNTIME_KIND_STATIC_PIE;
    if (pie)
    {
        dynamic_static_pie_inputs(&dynamic, &inputs, runtime, bind);
    }
    else
    {
        dynamic_static_inputs(&inputs, runtime, bind);
    }
    write->link = link;
    write->options = (ExecutableOptions){
        .path = path,
        .entry = dynamic_global(runtime, bind, "_start"),
        .dynamic = pie ? &dynamic : 0,
        .runtime = &inputs,
        .exact = write->exact,
    };
    write->succeeded = false;
    const bool ran = lane_run(lanes, dynamic_test_lane_main, write, 0);
    *result = write->result;
    return ran && write->succeeded;
}

// The writes every link gets: one per lane count of static_test_lane_counts, then the exact twins' with 1 and 8 lanes.
#define BUSTER_STATIC_TEST_WRITES (BUSTER_ARRAY_LENGTH(static_test_lane_counts) + 2)

static uint32_t static_test_write_lanes(uint32_t write)
{
    const uint32_t counts = BUSTER_ARRAY_LENGTH(static_test_lane_counts);
    return write < counts ? static_test_lane_counts[write] : write == counts ? 1 : 8;
}

// Writes the link BUSTER_STATIC_TEST_WRITES times, each from its bytes as binding left them (executable_test_save) with
// zeroed fields over the last file scribbled over, every other time over poisoned scratch memory: whether every write
// succeeded with the same bytes and build ID, which `file` then holds, read into `arena`, with the first write's result
// in `result`. A link without general- or local-dynamic TLS accesses (whose relaxation can't be repeated) is written
// once more over the bytes the last write left, its loads and calls relaxed already, and must come out the same:
// executable_relax_got takes the forms it writes, as a compiler writing a link again needs. The link is then as the
// last write left it, but for its bytes, which are binding's again: the checks read there the instructions a write
// rewrites.
static bool static_test_lanes(StaticTestTally* tally, Link* link, const Bind* bind, const Runtime* runtime, String8 path, const char* name, ExecutableResult* result,
                              String8* file, Arena* arena)
{
    bool same = true;
    *file = (String8){ 0 };
    const uint8_t* saved = executable_test_save(link, arena);
    static_test_expect(tally, saved != 0, "room for the atoms' bytes", 0);
    for (uint32_t w = 0; saved != 0 && w < BUSTER_STATIC_TEST_WRITES; w += 1)
    {
        executable_test_restore(link, saved);
        const uint32_t lanes = static_test_write_lanes(w);
        const bool exact = w >= BUSTER_ARRAY_LENGTH(static_test_lane_counts);
        ExecutableResult written;
        DynamicTestWrite write = {
            .exact = exact,
            .poison = w % 2 == 1,
        };
        const bool reset = executable_test_reset(link, path.pointer);
        const bool succeeded = reset && static_test_write_with(&write, link, bind, runtime, path, lanes, &written);
        static_test_expect(tally, succeeded, name, w);
        if (!succeeded)
        {
            executable_test_print_failure(name, &written);
        }
        const String8 bytes = succeeded ? executable_test_read(arena, path.pointer) : (String8){ 0 };
        const bool identical = w == 0 || (bytes.length == file->length && memcmp(bytes.pointer, file->pointer, bytes.length) == 0 &&
                                          memcmp(written.build_id, result->build_id, BUSTER_EXECUTABLE_BUILD_ID_BYTES) == 0);
        static_test_expect(tally, succeeded && identical, exact ? "the exact twins' file the same" : "the same file with every lane count", lanes);
        same = same && succeeded && identical;
        *file = w == 0 ? bytes : *file;
        *result = w == 0 ? written : *result;
    }
    bool accesses = false;
    for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
    {
        const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        accesses = accesses || kind == BUSTER_LINK_FIXUP_TLSGD || kind == BUSTER_LINK_FIXUP_TLSLD;
    }
    if (saved != 0 && same && !accesses)
    {
        ExecutableResult written;
        DynamicTestWrite write = { 0 };
        const bool succeeded = executable_test_reset(link, path.pointer) && static_test_write_with(&write, link, bind, runtime, path, 1, &written);
        const String8 bytes = succeeded ? executable_test_read(arena, path.pointer) : (String8){ 0 };
        const bool identical = bytes.length == file->length && memcmp(bytes.pointer, file->pointer, bytes.length) == 0;
        static_test_expect(tally, succeeded && identical, "a write over the relaxed bytes the same file", 0);
        same = same && succeeded && identical;
    }
    if (saved != 0)
    {
        executable_test_restore(link, saved);
    }
    return same && saved != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The structure
//
// What a static executable with the C runtime, or a static PIE, says, read back from its headers and sections and
// compared with the link after the write (its atoms' addresses, its symbols' GOT slots and references) by rules of the
// test's own.

// The file's IFUNC tables: its IRELATIVE relocations (a static executable's .rela.iplt; a static PIE's in .rela.dyn,
// after its RELATIVE ones, `relative`), .iplt's stubs and .got.
typedef struct StaticTestIfuncs StaticTestIfuncs;
struct StaticTestIfuncs
{
    const Elf64_Rela* relocations;
    uint64_t relocation_count;
    const Elf64_Rela* relative;
    uint64_t relative_count;
    const uint8_t* stubs;
    uint64_t stubs_address;
    uint64_t stub_count;
    const uint8_t* got;
    uint64_t got_address;
    uint64_t got_size;
};

static StaticTestIfuncs static_test_ifuncs(const ExecutableTestImage* image, String8 file, bool pie)
{
    const Elf64_Shdr* relocations = executable_test_section(image, pie ? ".rela.dyn" : ".rela.iplt");
    const Elf64_Shdr* stubs = executable_test_section(image, ".iplt");
    const Elf64_Shdr* got = executable_test_section(image, ".got");
    const bool relocations_inside = relocations != 0 && relocations->sh_offset + relocations->sh_size <= file.length;
    const bool stubs_inside = stubs != 0 && stubs->sh_offset + stubs->sh_size <= file.length;
    const bool got_inside = got != 0 && got->sh_offset + got->sh_size <= file.length;
    const Elf64_Rela* rela = relocations_inside ? (const Elf64_Rela*)(file.pointer + relocations->sh_offset) : 0;
    const uint64_t count = relocations_inside ? relocations->sh_size / sizeof(Elf64_Rela) : 0;
    uint64_t relative = 0;
    while (pie && relative < count && ELF64_R_TYPE(rela[relative].r_info) == R_X86_64_RELATIVE)
    {
        relative += 1;
    }
    const StaticTestIfuncs result = {
        .relocations = rela != 0 ? rela + relative : 0,
        .relocation_count = count - relative,
        .relative = rela,
        .relative_count = relative,
        .stubs = stubs_inside ? (const uint8_t*)file.pointer + stubs->sh_offset : 0,
        .stubs_address = stubs_inside ? stubs->sh_addr : 0,
        .stub_count = stubs_inside ? stubs->sh_size / BUSTER_EXECUTABLE_STUB_BYTES : 0,
        .got = got_inside ? (const uint8_t*)file.pointer + got->sh_offset : 0,
        .got_address = got_inside ? got->sh_addr : 0,
        .got_size = got_inside ? got->sh_size : 0,
    };
    return result;
}

// The resolver whose result .rela.iplt's IRELATIVE relocation of the slot at `slot` stores, or 0 if none relocates it.
static uint64_t static_test_irelative(const StaticTestIfuncs* ifuncs, uint64_t slot)
{
    uint64_t result = 0;
    for (uint64_t i = 0; i < ifuncs->relocation_count; i += 1)
    {
        const Elf64_Rela* relocation = &ifuncs->relocations[i];
        result = result == 0 && relocation->r_offset == slot && ELF64_R_TYPE(relocation->r_info) == R_X86_64_IRELATIVE ? (uint64_t)relocation->r_addend : result;
    }
    return result;
}

// The slot that the stub at `address` jumps through (jmp [rip + disp32], then a two-byte nop), or 0 if no stub starts
// there.
static uint64_t static_test_stub_slot(const StaticTestIfuncs* ifuncs, uint64_t address)
{
    const uint64_t index = ifuncs->stubs != 0 && address >= ifuncs->stubs_address ? (address - ifuncs->stubs_address) / BUSTER_EXECUTABLE_STUB_BYTES : UINT64_MAX;
    const bool stub = index < ifuncs->stub_count && address == ifuncs->stubs_address + BUSTER_EXECUTABLE_STUB_BYTES * index;
    const uint8_t* bytes = stub ? ifuncs->stubs + BUSTER_EXECUTABLE_STUB_BYTES * index : 0;
    int32_t distance = 0;
    if (stub)
    {
        memcpy(&distance, bytes + 2, sizeof(distance));
    }
    const bool form = stub && bytes[0] == 0xff && bytes[1] == 0x25 && bytes[6] == 0x66 && bytes[7] == 0x90;
    return form ? address + 6 + (uint64_t)(int64_t)distance : 0;
}

// Whether a fixup of `kind` reads its target's GOT slot.
static bool static_test_reads_slot(uint8_t kind)
{
    return kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL || kind == BUSTER_LINK_FIXUP_GOTTPOFF;
}

// The IFUNCs that a fixup which doesn't read a GOT slot reaches, which have stubs, a byte per symbol in `stubbed`:
// BUSTER_STATIC_TEST_CALLED for a call's, and with BUSTER_STATIC_TEST_TAKEN for another kind's, which takes the address
// (or the size), so that the stub stands for the IFUNC's address everywhere, as lld's canonical PLT entry does. The
// calls of __tls_get_addr (`tls_get_addr`) aren't such fixups: the TLS accesses consume them.
#define BUSTER_STATIC_TEST_CALLED 1
#define BUSTER_STATIC_TEST_TAKEN 2

static void static_test_stubbed(const Link* link, SymbolId tls_get_addr, uint8_t* stubbed)
{
    memset(stubbed, 0, link->symbols.count);
    for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
    {
        const SymbolId target = link->fixups.target[fixup];
        const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        const bool stub = !static_test_reads_slot(kind) && target != tls_get_addr && executable_test_is_ifunc(link, target);
        stubbed[target] |= stub ? (kind == BUSTER_LINK_FIXUP_CALL ? BUSTER_STATIC_TEST_CALLED : BUSTER_STATIC_TEST_TAKEN) : 0;
    }
}

// Whether an IFUNC's loads through the GOT read its stub's slot: its stub only calls reach (static_test_stubbed).
static bool static_test_reads_stub_slot(const uint8_t* stubbed, SymbolId symbol)
{
    return stubbed[symbol] == BUSTER_STATIC_TEST_CALLED;
}

// The IFUNCs, in an image at `base`: each one with a stub at a stub's address, which jumps through a slot that an
// IRELATIVE relocation fills from the IFUNC's resolver, its GOT slot, if it has one, holding the stub's address when a
// fixup other than a call takes it, and otherwise the stub's slot itself; each one without a stub but with a GOT slot,
// an IRELATIVE relocation of the slot; and no other relocation among them (.rela.iplt's, or those after a static PIE's
// RELATIVE ones), each an IRELATIVE of a slot of .got. Returns how many IRELATIVE relocations there must be.
static uint64_t static_test_ifunc_checks(StaticTestTally* tally, const Link* link, const StaticTestIfuncs* ifuncs, const uint8_t* stubbed, uint64_t base)
{
    uint64_t expected = 0;
    SymbolId wrong = 0;
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        if (executable_test_is_ifunc(link, symbol))
        {
            const uint64_t resolver = base + link->atoms.address[link->symbols.atom[symbol]] + link->symbols.value[symbol];
            const uint64_t stub = base + link->symbols.reference[symbol];
            const uint32_t got_slot = link->symbols.got_slot[symbol];
            const uint64_t slot = ifuncs->got_address + 8 * (uint64_t)got_slot;
            uint64_t held = 0;
            if (got_slot != 0 && ifuncs->got != 0 && 8 * ((uint64_t)got_slot + 1) <= ifuncs->got_size)
            {
                memcpy(&held, ifuncs->got + 8 * (uint64_t)got_slot, sizeof(held));
            }
            const uint64_t stub_slot = stubbed[symbol] != 0 ? static_test_stub_slot(ifuncs, stub) : 0;
            const bool slot_kind_right = static_test_reads_stub_slot(stubbed, symbol) ? slot == stub_slot : held == stub;
            const bool stub_right = stubbed[symbol] == 0 || (stub_slot != 0 && static_test_irelative(ifuncs, stub_slot) == resolver && (got_slot == 0 || slot_kind_right));
            const bool slot_right = stubbed[symbol] != 0 || got_slot == 0 || static_test_irelative(ifuncs, slot) == resolver;
            wrong = !(stub_right && slot_right) && wrong == 0 ? symbol : wrong;
            expected += stubbed[symbol] != 0 ? 1 : got_slot != 0 ? 1 : 0;
        }
    }
    static_test_expect(tally, wrong == 0, "each IFUNC through its stub or its slot, filled from its resolver", wrong);
    bool relocations = true;
    for (uint64_t i = 0; i < ifuncs->relocation_count; i += 1)
    {
        const Elf64_Rela* relocation = &ifuncs->relocations[i];
        relocations = relocations && ELF64_R_TYPE(relocation->r_info) == R_X86_64_IRELATIVE && ELF64_R_SYM(relocation->r_info) == 0 &&
                      relocation->r_offset >= ifuncs->got_address && relocation->r_offset + 8 <= ifuncs->got_address + ifuncs->got_size;
        for (uint64_t j = 0; j < i; j += 1)
        {
            relocations = relocations && ifuncs->relocations[j].r_offset != relocation->r_offset;
        }
    }
    static_test_expect(tally, relocations && ifuncs->relocation_count == expected, "an IRELATIVE relocation per IFUNC's slot, each a slot of .got's once", expected);
    return expected;
}

// The value a name of the linker's must have, from the file's section and program headers, as GNU ld and lld define
// it: false for a name the test doesn't know, or one whose section the file doesn't have (an empty one, which has no
// header to say where it is).
static bool static_test_linker_value(const Link* link, const ExecutableTestImage* image, SymbolId symbol, uint64_t* value)
{
    const Elf64_Shdr* got = executable_test_section(image, ".got");
    const Elf64_Shdr* init = executable_test_section(image, ".init_array");
    const Elf64_Shdr* fini = executable_test_section(image, ".fini_array");
    const Elf64_Shdr* irelative = executable_test_section(image, ".rela.iplt");
    const Elf64_Shdr* bss = executable_test_section(image, ".bss");
    uint64_t start = UINT64_MAX;
    uint64_t text_end = 0;
    uint64_t image_end = 0;
    for (uint32_t i = 0; image->valid && i < image->header->e_phnum; i += 1)
    {
        const Elf64_Phdr* segment = &image->program_headers[i];
        const bool load = segment->p_type == PT_LOAD;
        start = load && segment->p_offset == 0 ? segment->p_vaddr : start;
        text_end = load && (segment->p_flags & PF_X) != 0 ? segment->p_vaddr + segment->p_memsz : text_end;
        image_end = load && segment->p_vaddr + segment->p_memsz > image_end ? segment->p_vaddr + segment->p_memsz : image_end;
    }
    uint64_t data_end = 0;
    for (uint32_t i = 1; image->valid && i < image->header->e_shnum; i += 1)
    {
        const Elf64_Shdr* section = &image->section_headers[i];
        const bool bytes = (section->sh_flags & SHF_ALLOC) != 0 && section->sh_type != SHT_NOBITS;
        data_end = bytes && section->sh_addr + section->sh_size > data_end ? section->sh_addr + section->sh_size : data_end;
    }
    const Elf64_Shdr* dynamic = executable_test_section(image, ".dynamic");
    bool result = true;
    if (executable_named(link, symbol, "_GLOBAL_OFFSET_TABLE_"))
    {
        result = got != 0;
        *value = got != 0 ? got->sh_addr : 0;
    }
    else if (executable_named(link, symbol, "_DYNAMIC"))
    {
        result = dynamic != 0;
        *value = dynamic != 0 ? dynamic->sh_addr : 0;
    }
    else if (executable_named(link, symbol, "__init_array_start") || executable_named(link, symbol, "__preinit_array_start") ||
             executable_named(link, symbol, "__preinit_array_end"))
    {
        result = init != 0;
        *value = init != 0 ? init->sh_addr : 0;
    }
    else if (executable_named(link, symbol, "__init_array_end"))
    {
        result = init != 0;
        *value = init != 0 ? init->sh_addr + init->sh_size : 0;
    }
    else if (executable_named(link, symbol, "__fini_array_start") || executable_named(link, symbol, "__fini_array_end"))
    {
        result = fini != 0;
        *value = fini != 0 ? fini->sh_addr + (executable_named(link, symbol, "__fini_array_end") ? fini->sh_size : 0) : 0;
    }
    else if (executable_named(link, symbol, "__ehdr_start") || executable_named(link, symbol, "__executable_start"))
    {
        result = start != UINT64_MAX;
        *value = start;
    }
    else if (executable_named(link, symbol, "__rela_iplt_start") || executable_named(link, symbol, "__rela_iplt_end"))
    {
        result = irelative != 0;
        *value = irelative != 0 ? irelative->sh_addr + (executable_named(link, symbol, "__rela_iplt_end") ? irelative->sh_size : 0) : 0;
    }
    else if (executable_named(link, symbol, "_etext") || executable_named(link, symbol, "etext") || executable_named(link, symbol, "__etext"))
    {
        *value = text_end;
    }
    else if (executable_named(link, symbol, "_edata") || executable_named(link, symbol, "edata"))
    {
        *value = data_end;
    }
    else if (executable_named(link, symbol, "__bss_start"))
    {
        result = bss != 0;
        *value = bss != 0 ? bss->sh_addr : 0;
    }
    else if (executable_named(link, symbol, "_end") || executable_named(link, symbol, "end"))
    {
        *value = image_end;
    }
    else
    {
        result = false;
    }
    return result;
}

// The value a fixup's target has in the file, in an image at `base`: an IFUNC with a stub its stub's address (its
// reference, which static_test_ifunc_checks checked), a name of the linker's static_test_linker_value's, an absolute
// symbol its value, and any other test-executable's (executable_test_symbol_value: a thread-local symbol its offset
// from the thread pointer). False if the test can't tell.
static bool static_test_symbol_value(const Link* link, const ExecutableTestImage* image, const uint8_t* stubbed, SymbolId symbol, uint64_t* value, uint64_t base)
{
    const uint8_t source = link->symbols.source[symbol];
    bool result = true;
    if (stubbed[symbol] != 0)
    {
        *value = base + link->symbols.reference[symbol];
    }
    else if (source == BUSTER_LINK_SOURCE_LINKER)
    {
        result = static_test_linker_value(link, image, symbol, value);
    }
    else if (source == BUSTER_LINK_SOURCE_ABSOLUTE)
    {
        *value = link->symbols.value[symbol];
    }
    else
    {
        result = executable_test_symbol_value(link, image, symbol, value, base);
    }
    return result;
}

// The bytes fixup `fixup` of `holder` must leave there, in an image at `base`: `*length` bytes (up to 16) from `*start`
// in the atom. A general- or local-dynamic access leaves its local-exec sequence over its lea and its call (whose own
// fixup leaves nothing), and DTPOFF32 the offset from the thread pointer; a load or call that relaxes
// (executable_test_relaxes) its instruction rewritten from the one in the link's bytes (as binding left them):
// mov to lea, call to addr32 call, jmp to jmp and a nop; the other kinds leave their field, as executable_test_expected
// works it out. False if the test can't tell, or a value can't fit: a link the tests write has none.
static bool static_test_expected(const Link* link, const ExecutableTestImage* image, const uint8_t* stubbed, SymbolId tls_get_addr, AtomId holder, FixupId fixup,
                                 uint64_t base, uint8_t* expected, uint32_t* length, uint64_t* start)
{
    static const uint8_t gd_le[12] = { 0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x80 };
    static const uint8_t ld_le[13] = { 0x66, 0x66, 0x66, 0x66, 0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00 };
    const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
    const SymbolId target = link->fixups.target[fixup];
    const uint64_t offset = link->fixups.offset[fixup];
    const FixupId last = link->atoms.fixup_begin[holder] + link->atoms.fixup_count[holder] - 1;
    const uint8_t next_kind = fixup < last ? link->fixups.kind[fixup + 1] & BUSTER_LINK_FIXUP_KIND : 0;
    const Elf64_Shdr* got = executable_test_section(image, ".got");
    uint64_t symbol = 0;
    bool result = static_test_symbol_value(link, image, stubbed, target, &symbol, base);
    const uint64_t place = base + link->atoms.address[holder] + offset;
    const uint64_t slot = got != 0 ? got->sh_addr + 8 * (uint64_t)link->symbols.got_slot[target] : 0;
    const uint64_t addend = (uint64_t)link_fixup_addend(link, fixup);
    uint64_t value = 0;
    uint32_t width = 4;
    // 0 for a signed 32-bit field, 1 for an unsigned one, 2 for 64 bits.
    uint32_t range = 0;
    *start = offset;
    if (target == tls_get_addr && tls_get_addr != 0)
    {
        width = 0;
    }
    else if (kind == BUSTER_LINK_FIXUP_TLSGD)
    {
        const int32_t tpoff = (int32_t)symbol;
        memcpy(expected, gd_le, sizeof(gd_le));
        memcpy(expected + sizeof(gd_le), &tpoff, sizeof(tpoff));
        width = 16;
        *start = offset - 4;
        result = result && offset >= 4 && (int64_t)symbol == tpoff;
    }
    else if (kind == BUSTER_LINK_FIXUP_TLSLD)
    {
        // Three prefixes before mov rax, fs:0 for a call through the PLT, which leaves 12 bytes, four through the GOT.
        const uint32_t prefixes = next_kind == BUSTER_LINK_FIXUP_CALL ? 1 : 0;
        memcpy(expected, ld_le + prefixes, sizeof(ld_le) - prefixes);
        width = (uint32_t)sizeof(ld_le) - prefixes;
        *start = offset - 3;
        result = result && offset >= 3;
    }
    else if (executable_test_relaxes(link, fixup))
    {
        // The 6 bytes from the instruction's opcode.
        width = 6;
        *start = offset - 2;
        result = result && executable_test_relaxed(link, holder, fixup, symbol, place, expected);
    }
    else
    {
        switch (kind)
        {
            case BUSTER_LINK_FIXUP_CALL:
            case BUSTER_LINK_FIXUP_PC32:
                value = symbol + addend - place;
                break;
            case BUSTER_LINK_FIXUP_CALL_GOT:
            case BUSTER_LINK_FIXUP_LOAD_GOT:
            case BUSTER_LINK_FIXUP_GOTPCREL:
            case BUSTER_LINK_FIXUP_GOTTPOFF:
                value = slot + addend - place;
                break;
            case BUSTER_LINK_FIXUP_TPOFF32:
            case BUSTER_LINK_FIXUP_DTPOFF32:
            case BUSTER_LINK_FIXUP_ABS32S:
                value = symbol + addend;
                break;
            case BUSTER_LINK_FIXUP_ABS32:
                value = symbol + addend;
                range = 1;
                break;
            case BUSTER_LINK_FIXUP_SIZE32:
                value = link->symbols.size[target] + addend;
                range = 1;
                break;
            case BUSTER_LINK_FIXUP_ABS64:
                value = symbol + addend;
                range = 2;
                break;
            case BUSTER_LINK_FIXUP_PC64:
                value = symbol + addend - place;
                range = 2;
                break;
            case BUSTER_LINK_FIXUP_SIZE64:
                value = link->symbols.size[target] + addend;
                range = 2;
                break;
            default:
                result = false;
                break;
        }
        width = range == 2 ? 8 : 4;
        result = result && (range == 2 || (range == 1 && value <= UINT32_MAX) || (range == 0 && (int64_t)value == (int32_t)value));
        memcpy(expected, &value, width);
    }
    *length = width;
    return result;
}

// Every fixup's bytes in the file are static_test_expected's, and a load or call through the GOT that doesn't relax
// keeps its instruction; and the GOT: a slot for each target of a GOT kind (but __tls_get_addr, whose calls the
// accesses consumed, the fixups that relax and the IFUNCs whose stubs only calls reach, which read their stubs' slots)
// and no other symbol, numbered from 1 in symbol order, each holding its symbol's value (an IFUNC's stub, a thread-local
// symbol's offset from the thread pointer) but an IFUNC's without a stub, which an IRELATIVE relocation fills; then one
// per stub, which static_test_ifunc_checks counted. Returns how many loads and calls relaxed.
static uint64_t static_test_fields(StaticTestTally* tally, const Link* link, const ExecutableTestImage* image, String8 file, const uint8_t* stubbed, SymbolId tls_get_addr,
                                   uint64_t stubs, bool pie, Arena* arena)
{
    const uint64_t base = static_test_base(pie);
    uint64_t mismatched = 0;
    uint64_t relaxed = 0;
    FixupId first = 0;
    for (AtomId atom = 1; image->valid && atom < link->atoms.count; atom += 1)
    {
        const FixupId begin = link->atoms.fixup_begin[atom];
        const uint64_t atom_offset = executable_test_file_offset(image, base + link->atoms.address[atom]);
        for (FixupId fixup = begin; fixup < begin + link->atoms.fixup_count[atom]; fixup += 1)
        {
            uint8_t expected[16];
            uint32_t length = 0;
            uint64_t start = 0;
            const bool known = static_test_expected(link, image, stubbed, tls_get_addr, atom, fixup, base, expected, &length, &start);
            const bool readable = atom_offset != UINT64_MAX && atom_offset + start + length <= file.length;
            // A call of __tls_get_addr through the GOT is a TLS access's, whose relaxation rewrote it.
            const bool relaxes = executable_test_relaxes(link, fixup);
            const bool consumed = tls_get_addr != 0 && link->fixups.target[fixup] == tls_get_addr;
            const bool kept = relaxes || consumed ||
                              (readable && executable_test_unrelaxed(link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND, (const uint8_t*)file.pointer + atom_offset, link->fixups.offset[fixup]));
            const bool same = known && readable && kept && memcmp(file.pointer + atom_offset + start, expected, length) == 0;
            first = !same && mismatched == 0 ? fixup : first;
            mismatched += same ? 0 : 1;
            relaxed += relaxes ? 1 : 0;
        }
    }
    static_test_expect(tally, mismatched == 0, "every fixup's bytes", first);
    const uint64_t length = arena->length;
    uint8_t* targets = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, link->symbols.count);
    const Elf64_Shdr* got = executable_test_section(image, ".got");
    bool slots = targets != 0;
    if (slots)
    {
        memset(targets, 0, link->symbols.count);
        for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
        {
            const SymbolId target = link->fixups.target[fixup];
            const bool relaxes = executable_test_relaxes(link, fixup);
            targets[target] |= static_test_reads_slot(link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) && target != tls_get_addr && !relaxes ? 1 : 0;
        }
    }
    // The slots of their own come first, then the stubs': an IFUNC whose stub only calls reach reads its stub's slot
    // through the GOT, the stub's index after them, which holds 0 until its IRELATIVE relocation fills it.
    uint32_t own_slots = 0;
    for (SymbolId symbol = 1; slots && symbol < link->symbols.count; symbol += 1)
    {
        own_slots += targets[symbol] != 0 && !static_test_reads_stub_slot(stubbed, symbol) ? 1 : 0;
    }
    const Elf64_Shdr* iplt = executable_test_section(image, ".iplt");
    uint32_t next = 1;
    SymbolId wrong = 0;
    for (SymbolId symbol = 1; slots && image->valid && symbol < link->symbols.count; symbol += 1)
    {
        const uint32_t slot = link->symbols.got_slot[symbol];
        uint64_t expected = 0;
        uint64_t actual = 0;
        const bool known = static_test_symbol_value(link, image, stubbed, symbol, &expected, base);
        const uint64_t offset = got != 0 ? got->sh_offset + 8 * (uint64_t)slot : 0;
        const bool inside = got != 0 && 8 * ((uint64_t)slot + 1) <= got->sh_size && offset + 8 <= file.length;
        if (slot != 0 && inside)
        {
            memcpy(&actual, file.pointer + offset, sizeof(actual));
        }
        const bool filled = executable_test_is_ifunc(link, symbol) && stubbed[symbol] == 0;
        const bool shares = targets[symbol] != 0 && static_test_reads_stub_slot(stubbed, symbol);
        const uint64_t stub = base + link->symbols.reference[symbol];
        const uint64_t stub_index = iplt != 0 && stub >= iplt->sh_addr ? (stub - iplt->sh_addr) / BUSTER_EXECUTABLE_STUB_BYTES : UINT64_MAX;
        const bool own = targets[symbol] != 0 && !shares;
        const bool right = shares ? stub_index < stubs && slot == own_slots + 1 + stub_index && inside && actual == 0
                                  : (slot != 0) == own && (slot == 0 || (slot == next && inside && (filled || !known || actual == expected)));
        wrong = !right && wrong == 0 ? symbol : wrong;
        next += own ? 1 : 0;
    }
    slots = slots && wrong == 0 && (got == 0 ? next == 1 && stubs == 0 : got->sh_size == 8 * ((uint64_t)next + stubs));
    static_test_expect(tally, slots, "a GOT slot for each GOT kind's target in symbol order but an IFUNC's whose stub only calls reach, then one per stub", wrong);
    arena_pop_to(arena, length);
    return relaxed;
}

// An array's atom and where it must go: its priority (UINT16_MAX + 1 for none, after every priority), then its id.
typedef struct StaticTestEntry StaticTestEntry;
struct StaticTestEntry
{
    uint32_t key;
    AtomId atom;
};

static int static_test_compare_entries(const void* a, const void* b)
{
    const StaticTestEntry* x = a;
    const StaticTestEntry* y = b;
    return x->key != y->key ? (x->key < y->key ? -1 : 1) : x->atom < y->atom ? -1 : x->atom > y->atom;
}

// The init and fini arrays, in an image at `base`: each class's atoms in the order GNU ld and lld put them, the
// prioritized ones by priority and in id order for equal ones, then the others in id order, one after another from the
// section's start to its end.
static void static_test_arrays(StaticTestTally* tally, const Link* link, const ExecutableTestImage* image, uint64_t base, Arena* arena)
{
    static const char* const sections[2] = {
        ".init_array",
        ".fini_array",
    };
    for (uint32_t array = 0; array < 2; array += 1)
    {
        const uint8_t class = array == 0 ? BUSTER_LINK_CLASS_INIT_ARRAY : BUSTER_LINK_CLASS_FINI_ARRAY;
        const uint64_t length = arena->length;
        StaticTestEntry* entries = BUSTER_ARENA_PUSH_ARRAY(arena, StaticTestEntry, link->atoms.count);
        uint32_t count = 0;
        uint32_t priority = 1;
        for (AtomId atom = 1; entries != 0 && atom < link->atoms.count; atom += 1)
        {
            while (priority < link->priorities.count && link->priorities.atom[priority] < atom)
            {
                priority += 1;
            }
            const bool prioritized = priority < link->priorities.count && link->priorities.atom[priority] == atom;
            if (link->atoms.class[atom] == class)
            {
                entries[count] = (StaticTestEntry){
                    .key = prioritized ? link->priorities.priority[priority] : UINT16_MAX + 1u,
                    .atom = atom,
                };
                count += 1;
            }
        }
        if (entries != 0)
        {
            qsort(entries, count, sizeof(StaticTestEntry), static_test_compare_entries);
        }
        const Elf64_Shdr* section = executable_test_section(image, sections[array]);
        uint64_t address = section != 0 ? section->sh_addr - base : 0;
        bool ordered = entries != 0 && (count == 0 || section != 0);
        for (uint32_t i = 0; ordered && i < count; i += 1)
        {
            ordered = link->atoms.address[entries[i].atom] == address;
            address += link->atoms.size[entries[i].atom];
        }
        ordered = ordered && (count == 0 || address == section->sh_addr - base + section->sh_size);
        static_test_expect(tally, ordered, array == 0 ? ".init_array's atoms by priority, then in id order" : ".fini_array's atoms by priority, then in id order", count);
        arena_pop_to(arena, length);
    }
}

// The headers: an executable that starts at _start, without a program interpreter, with the start file's ABI note
// (crt1.o's or rcrt1.o's) in a PT_NOTE, and PT_TLS over .tdata and .tbss; a static executable without .dynamic, and a
// static PIE with PT_DYNAMIC over it and lld's tags there (static_test_dynamic).
static void static_test_headers(StaticTestTally* tally, const Link* link, const ExecutableTestImage* image, String8 file, const Runtime* runtime, SymbolId entry)
{
    const bool pie = static_test_pie(runtime);
    const AtomId entry_atom = entry != 0 ? link->symbols.atom[entry] : 0;
    static_test_expect(tally,
                       image->valid && entry_atom != 0 && image->header->e_entry == static_test_base(pie) + link->atoms.address[entry_atom] + link->symbols.value[entry],
                       "an executable that starts at _start", 0);
    const Elf64_Phdr* dynamic = executable_test_segment(image, PT_DYNAMIC);
    const Elf64_Shdr* dynamic_section = executable_test_section(image, ".dynamic");
    const bool dynamic_right = pie ? dynamic != 0 && dynamic_section != 0 && dynamic->p_vaddr == dynamic_section->sh_addr && dynamic->p_filesz == dynamic_section->sh_size
                                   : dynamic == 0 && dynamic_section == 0;
    static_test_expect(tally, executable_test_segment(image, PT_INTERP) == 0 && dynamic_right, pie ? "no PT_INTERP, PT_DYNAMIC .dynamic" : "no PT_INTERP or PT_DYNAMIC", 0);
    bool note = false;
    for (uint32_t i = 0; image->valid && i < image->header->e_phnum; i += 1)
    {
        const Elf64_Phdr* segment = &image->program_headers[i];
        const bool inside = segment->p_type == PT_NOTE && segment->p_offset + segment->p_filesz <= file.length && runtime->abi_note.length != 0;
        for (uint64_t at = 0; inside && at + runtime->abi_note.length <= segment->p_filesz; at += 4)
        {
            note = note || memcmp(file.pointer + segment->p_offset + at, runtime->abi_note.pointer, runtime->abi_note.length) == 0;
        }
    }
    static_test_expect(tally, note, "the start file's ABI note in a PT_NOTE", 0);
    const Elf64_Shdr* tdata = executable_test_section(image, ".tdata");
    const Elf64_Shdr* tbss = executable_test_section(image, ".tbss");
    const Elf64_Phdr* tls = executable_test_segment(image, PT_TLS);
    const uint64_t start = tdata != 0 ? tdata->sh_addr : tbss != 0 ? tbss->sh_addr : 0;
    const uint64_t end = tbss != 0 ? tbss->sh_addr + tbss->sh_size : tdata != 0 ? tdata->sh_addr + tdata->sh_size : 0;
    const bool tls_right = (tdata == 0 && tbss == 0) ? tls == 0
                                                     : tls != 0 && tls->p_vaddr == start && tls->p_memsz == end - start && tls->p_filesz == (tdata != 0 ? tdata->sh_size : 0);
    static_test_expect(tally, tls_right, "PT_TLS over .tdata and .tbss", 0);
}

// A static PIE's .dynamic, with lld's tags: DT_FLAGS_1 PIE, without DT_NEEDED or versions; .dynsym with the null
// symbol alone and its empty strings; .gnu.hash; .rela.dyn's address and size, and DT_RELACOUNT its RELATIVE
// relocations (`relative`), and .relr.dyn's; the init and fini arrays'; and DT_INIT and DT_FINI crti.o's functions.
static void static_test_dynamic(StaticTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, String8 file, uint64_t relative)
{
    const DynamicTestImage image = dynamic_test_image(file);
    uint64_t value = 0;
    static_test_expect(tally, image.valid && dynamic_test_tag(&image, DT_FLAGS_1, &value) && value == DF_1_PIE, "DT_FLAGS_1 PIE", 0);
    static_test_expect(tally, !dynamic_test_tag(&image, DT_NEEDED, &value) && !dynamic_test_tag(&image, DT_VERNEED, &value) && !dynamic_test_tag(&image, DT_VERSYM, &value),
                       "no DT_NEEDED or versions", 0);
    const struct
    {
        int64_t tag;
        const char* section;
        bool size;
    } tables[] = {
        { DT_SYMTAB, ".dynsym", false },
        { DT_STRTAB, ".dynstr", false },
        { DT_STRSZ, ".dynstr", true },
        { DT_GNU_HASH, ".gnu.hash", false },
        { DT_RELA, ".rela.dyn", false },
        { DT_RELASZ, ".rela.dyn", true },
        { DT_RELR, ".relr.dyn", false },
        { DT_RELRSZ, ".relr.dyn", true },
        { DT_INIT_ARRAY, ".init_array", false },
        { DT_INIT_ARRAYSZ, ".init_array", true },
        { DT_FINI_ARRAY, ".fini_array", false },
        { DT_FINI_ARRAYSZ, ".fini_array", true },
    };
    for (uint32_t t = 0; image.valid && t < BUSTER_ARRAY_LENGTH(tables); t += 1)
    {
        const Elf64_Shdr* section = dynamic_test_section(&image, tables[t].section);
        const bool tagged = dynamic_test_tag(&image, tables[t].tag, &value);
        static_test_expect(tally, section != 0 ? tagged && value == (tables[t].size ? section->sh_size : section->sh_addr) : !tagged, tables[t].section, t);
    }
    const Elf64_Shdr* dynsym = dynamic_test_section(&image, ".dynsym");
    const Elf64_Shdr* dynstr = dynamic_test_section(&image, ".dynstr");
    static const Elf64_Sym null = { 0 };
    static_test_expect(tally,
                       dynsym != 0 && dynsym->sh_size == sizeof(Elf64_Sym) && memcmp(dynamic_test_bytes(&image, dynsym), &null, sizeof(null)) == 0 && dynstr != 0 &&
                           dynstr->sh_size == 1 && dynamic_test_bytes(&image, dynstr)[0] == 0,
                       ".dynsym's null symbol alone", 0);
    const bool counted = dynamic_test_tag(&image, DT_RELACOUNT, &value);
    static_test_expect(tally, relative != 0 ? counted && value == relative : !counted, "DT_RELACOUNT", relative);
    const SymbolId init = dynamic_global(runtime, bind, "_init");
    const SymbolId fini = dynamic_global(runtime, bind, "_fini");
    static_test_expect(tally, dynamic_test_tag(&image, DT_INIT, &value) && init != 0 && value == dynamic_test_address(link, init), "DT_INIT _init", 0);
    static_test_expect(tally, dynamic_test_tag(&image, DT_FINI, &value) && fini != 0 && value == dynamic_test_address(link, fini), "DT_FINI _fini", 0);
}

// A relocation the test expects or the file has: where, its addend, and whether .relr.dyn holds it (whose addend is the
// field's bytes) rather than .rela.dyn.
typedef struct StaticTestRelocation StaticTestRelocation;
struct StaticTestRelocation
{
    uint64_t offset;
    int64_t addend;
    uint64_t packed;
};

static int static_test_by_offset(const void* a, const void* b)
{
    const StaticTestRelocation* x = a;
    const StaticTestRelocation* y = b;
    return x->offset != y->offset ? (x->offset < y->offset ? -1 : 1) : x->addend < y->addend ? -1 : x->addend > y->addend;
}

// A static PIE's RELATIVE relocations, by the test's own rules: one of each GOT slot that holds an address in the image
// (a symbol of the image's, executable_test_in_image, or an IFUNC's stub when the stub stands for its address: an
// IFUNC's slot without a stub has an IRELATIVE relocation instead, and one whose stub only calls reach reads the stub's
// slot, which has one), its addend the slot's value; one of each ABS64 field of an atom the loader writes (.tdata, the
// init and fini arrays, .data.rel.ro and .data) whose target is in the image, its addend the target's value and the
// fixup's addend; and no others. With .relr.dyn (`relr`), it holds those of the slots and of the fields at even addresses
// in atoms aligned to 2 or more, each field holding its addend, and .rela.dyn the others, first (`ifuncs->relative`).
static void static_test_relative(StaticTestTally* tally, const Link* link, const ExecutableTestImage* image, String8 file, const StaticTestIfuncs* ifuncs,
                                 const uint8_t* stubbed, bool relr, Arena* arena)
{
    const uint64_t length = arena->length;
    const uint64_t capacity = (uint64_t)link->symbols.count + link->fixups.count;
    StaticTestRelocation* expected = BUSTER_ARENA_PUSH_ARRAY(arena, StaticTestRelocation, capacity);
    StaticTestRelocation* actual = BUSTER_ARENA_PUSH_ARRAY(arena, StaticTestRelocation, capacity + ifuncs->relative_count + 1);
    uint64_t* relr_addresses = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, capacity + 1);
    static_test_expect(tally, expected != 0 && actual != 0 && relr_addresses != 0, "room for the relocations", 0);
    uint64_t wanted = 0;
    bool known = true;
    for (SymbolId symbol = 1; expected != 0 && symbol < link->symbols.count; symbol += 1)
    {
        const uint32_t slot = link->symbols.got_slot[symbol];
        const bool filled = executable_test_is_ifunc(link, symbol) && (stubbed[symbol] == 0 || static_test_reads_stub_slot(stubbed, symbol));
        uint64_t value = 0;
        if (slot != 0 && executable_test_in_image(link, symbol) && !filled)
        {
            known = known && static_test_symbol_value(link, image, stubbed, symbol, &value, 0);
            expected[wanted] = (StaticTestRelocation){
                .offset = ifuncs->got_address + 8 * (uint64_t)slot,
                .addend = (int64_t)value,
                .packed = relr,
            };
            wanted += 1;
        }
    }
    for (AtomId atom = 1; expected != 0 && atom < link->atoms.count; atom += 1)
    {
        const uint8_t class = link->atoms.class[atom];
        const bool written = class == BUSTER_LINK_CLASS_TDATA || class == BUSTER_LINK_CLASS_INIT_ARRAY || class == BUSTER_LINK_CLASS_FINI_ARRAY ||
                             class == BUSTER_LINK_CLASS_DATA_RELRO || class == BUSTER_LINK_CLASS_DATA;
        const FixupId begin = link->atoms.fixup_begin[atom];
        for (FixupId fixup = begin; written && fixup < begin + link->atoms.fixup_count[atom]; fixup += 1)
        {
            const SymbolId target = link->fixups.target[fixup];
            uint64_t value = 0;
            if ((link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_ABS64 && executable_test_in_image(link, target))
            {
                known = known && static_test_symbol_value(link, image, stubbed, target, &value, 0);
                const uint64_t offset = link->atoms.address[atom] + link->fixups.offset[fixup];
                expected[wanted] = (StaticTestRelocation){
                    .offset = offset,
                    .addend = (int64_t)value + link_fixup_addend(link, fixup),
                    .packed = relr && link->atoms.alignment_log2[atom] >= 1 && offset % 2 == 0,
                };
                wanted += 1;
            }
        }
    }
    // The file's: .rela.dyn's RELATIVE ones, then .relr.dyn's addresses, each with its field's bytes.
    const DynamicTestImage dynamic = dynamic_test_image(file);
    const uint64_t relr_count = relr_addresses != 0 ? dynamic_test_relr(&dynamic, relr_addresses, capacity + 1) : UINT64_MAX;
    static_test_expect(tally, relr_count != UINT64_MAX && (relr || relr_count == 0), ".relr.dyn read, and only with RELR", relr_count);
    const uint64_t total = ifuncs->relative_count + (relr_count != UINT64_MAX ? relr_count : 0);
    bool same = expected != 0 && actual != 0 && relr_count != UINT64_MAX && known && wanted == total;
    for (uint64_t i = 0; same && i < ifuncs->relative_count; i += 1)
    {
        same = ELF64_R_SYM(ifuncs->relative[i].r_info) == 0;
        actual[i] = (StaticTestRelocation){
            .offset = ifuncs->relative[i].r_offset,
            .addend = ifuncs->relative[i].r_addend,
            .packed = false,
        };
    }
    for (uint64_t i = 0; same && i < relr_count; i += 1)
    {
        const uint64_t file_offset = dynamic_test_file_offset(&dynamic, relr_addresses[i]);
        int64_t field = 0;
        if (file_offset != UINT64_MAX)
        {
            memcpy(&field, file.pointer + file_offset, sizeof(field));
        }
        same = file_offset != UINT64_MAX;
        actual[ifuncs->relative_count + i] = (StaticTestRelocation){
            .offset = relr_addresses[i],
            .addend = field,
            .packed = true,
        };
    }
    if (same)
    {
        qsort(expected, wanted, sizeof(*expected), static_test_by_offset);
        qsort(actual, wanted, sizeof(*actual), static_test_by_offset);
        same = memcmp(expected, actual, wanted * sizeof(*expected)) == 0;
    }
    static_test_expect(tally, same, "a RELATIVE relocation of each slot and written ABS64 field that holds an address in the image, and no others", wanted);
    arena_pop_to(arena, length);
}

// Every rule above, of a file `file` that executable_write (`result`) wrote of `link`, bound by `bind` against
// `runtime`: a static executable's, or a static PIE's. Returns how many loads and calls through the GOT relaxed.
static uint64_t static_test_structure(StaticTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, String8 file, const ExecutableResult* result,
                                      Arena* arena)
{
    uint64_t relaxed = 0;
    const bool pie = static_test_pie(runtime);
    const uint64_t base = static_test_base(pie);
    const ExecutableTestImage image = executable_test_image_of(file, pie ? ET_DYN : ET_EXEC);
    static_test_expect(tally, image.valid, pie ? "the file reads as an ET_DYN" : "the file reads as an ET_EXEC", 0);
    ExecutableRuntime inputs;
    dynamic_static_inputs(&inputs, runtime, bind);
    const uint64_t length = arena->length;
    uint8_t* stubbed = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, link->symbols.count);
    static_test_expect(tally, stubbed != 0, "room for the IFUNCs' marks", 0);
    if (image.valid && stubbed != 0)
    {
        static_test_headers(tally, link, &image, file, runtime, dynamic_global(runtime, bind, "_start"));
        static_test_stubbed(link, inputs.tls_get_addr, stubbed);
        const StaticTestIfuncs ifuncs = static_test_ifuncs(&image, file, pie);
        const uint64_t irelatives = static_test_ifunc_checks(tally, link, &ifuncs, stubbed, base);
        static_test_expect(tally,
                           result->relocations == irelatives + ifuncs.relative_count && result->relative_relocations == ifuncs.relative_count &&
                               result->stubs == ifuncs.stub_count,
                           "the result's counts of relocations and stubs", result->relocations);
        if (pie)
        {
            ExecutableDynamic dynamic_inputs;
            ExecutableRuntime pie_inputs;
            dynamic_static_pie_inputs(&dynamic_inputs, &pie_inputs, runtime, bind);
            static_test_dynamic(tally, link, bind, runtime, file, ifuncs.relative_count);
            static_test_relative(tally, link, &image, file, &ifuncs, stubbed, dynamic_inputs.relr, arena);
        }
        relaxed = static_test_fields(tally, link, &image, file, stubbed, inputs.tls_get_addr, ifuncs.stub_count, pie, arena);
        // Each name of the linker's where its section or segment says, and a static executable's __rela_iplt_start and
        // __rela_iplt_end alike without IRELATIVE relocations.
        SymbolId wrong = 0;
        uint64_t iplt[2] = {
            UINT64_MAX,
            UINT64_MAX,
        };
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            uint64_t value = 0;
            const bool linker = link->symbols.source[symbol] == BUSTER_LINK_SOURCE_LINKER;
            const bool known = linker && static_test_linker_value(link, &image, symbol, &value);
            wrong = known && base + link->symbols.reference[symbol] != value && wrong == 0 ? symbol : wrong;
            iplt[0] = linker && executable_named(link, symbol, "__rela_iplt_start") ? link->symbols.reference[symbol] : iplt[0];
            iplt[1] = linker && executable_named(link, symbol, "__rela_iplt_end") ? link->symbols.reference[symbol] : iplt[1];
        }
        static_test_expect(tally, wrong == 0 && (pie || ifuncs.relocation_count != 0 || iplt[0] == iplt[1]), "the linker's names where its sections and segments say", wrong);
        static_test_arrays(tally, link, &image, base, arena);
        const char* unwind = executable_test_unwind(link, file, base, bind->unit_base, result);
        static_test_expect(tally, unwind == 0, unwind != 0 ? unwind : "the unwind tables", 0);
    }
    arena_pop_to(arena, length);
    return relaxed;
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs

// test-link's sets of options, and one more that calls libc with --functions workers.
#define BUSTER_STATIC_TEST_PROGRAMS (BUSTER_LINK_TEST_PROGRAMS + 1)

static bool static_test_programs(const ExecutableTestDirectory* directory, const Runtime* runtime, uint64_t seed, uint32_t functions)
{
    StaticTestTally tally = {
        .check = static_test_pie(runtime) ? "programs, static PIEs" : "programs",
    };
    LinkProgramOptions options[BUSTER_STATIC_TEST_PROGRAMS];
    link_test_program_options(options, seed, functions);
    options[BUSTER_LINK_TEST_PROGRAMS] = options[5];
    options[BUSTER_LINK_TEST_PROGRAMS].seed = seed + BUSTER_LINK_TEST_PROGRAMS;
    options[BUSTER_LINK_TEST_PROGRAMS].functions = functions;
    for (uint32_t i = 0; i < BUSTER_STATIC_TEST_PROGRAMS; i += 1)
    {
        const bool workers = i == BUSTER_LINK_TEST_PROGRAMS;
        const char* name = workers ? "calling libc, --functions workers" : link_test_program_names[i];
        const char* directory_name = workers ? "libc-functions" : link_test_program_directories[i];
        if (options[i].libc)
        {
            BindTestProgram bound;
            const bool built = bind_test_program(&bound, runtime, options[i], BIND_PATH_VECTOR) && link_check(&bound.program.link) == LINK_ERROR_NONE;
            static_test_expect(&tally, built, "the program bound", i);
            char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
            const String8 path = executable_test_path(directory, directory_name, path_bytes);
            Scratch scratch = scratch_begin(0);
            ExecutableResult result = { 0 };
            String8 file = { 0 };
            const bool written = built && static_test_lanes(&tally, &bound.program.link, &bound.bind, runtime, path, name, &result, &file, scratch.arena);
            const uint64_t relaxed = written ? static_test_structure(&tally, &bound.program.link, &bound.bind, runtime, file, &result, scratch.arena) : 0;
            const bool ran = written && executable_test_runs(path.pointer, bound.program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES);
            static_test_expect(&tally, ran, "the program runs as its model predicts", i);
            printf("  %s: %u atoms, %u fixups, %llu stubs, %llu relocations (%llu RELATIVE), %llu loads and calls relaxed, %llu GOT slots, %llu FDEs, %llu bytes: %s\n",
                   name, bound.program.link.atoms.count - 1, bound.program.link.fixups.count - 1, (unsigned long long)result.stubs,
                   (unsigned long long)result.relocations, (unsigned long long)result.relative_relocations, (unsigned long long)relaxed, (unsigned long long)result.got_slots,
                   (unsigned long long)result.fdes, (unsigned long long)result.file_size, ran ? "ran" : "didn't run");
            scratch_end(scratch);
            if (!directory->keep)
            {
                unlink(path.pointer);
            }
            bind_test_release(&bound);
        }
    }
    return static_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The compiled programs

// A program that cc compiles, or assembles: its name, its source, an option for cc and for the link, a condition for
// cc to meet, a preprocessor expression (0 for none), without which the program isn't compiled, assembly that defines
// absolute symbols (0 for none), which an object of its own holds (static_test_combine), and whether lld links it
// wrong, so that the cross-check only has lld link it. As a static PIE: the status it must run with where GNU ld's
// link runs otherwise (0 when they agree), or whether a static PIE refuses it (EXECUTABLE_ERROR_RELOCATION), as
// cc -static-pie does.
typedef struct StaticTestSource StaticTestSource;
struct StaticTestSource
{
    const char* name;
    const char* source;
    const char* option;
    const char* condition;
    const char* definitions;
    bool assembly;
    bool lld_wrong;
    int pie_status;
    bool pie_refused;
};

// test-bind's C programs come first (bind_test_static_sources), then these:
//   - priorities: constructors and destructors with priorities and without, around libgcc's cpuinfo.o's constructor
//     (priority 101), which __builtin_cpu_supports needs;
//   - decimal: decimal floating point, whose rounding mode and flags are libgcc's thread-local variables, reached by
//     general-dynamic accesses (libgcc is PIC), also from a thread that sets its own rounding mode;
//   - nested: a nested function called through a pointer, which GCC 14's heap trampolines make (libgcc_eh.a's
//     heap-trampoline.o, whose thread-local state its local-dynamic accesses reach);
//   - tls: each general- and local-dynamic sequence, through the PLT and through the GOT, two local-dynamic offsets from
//     one module's block among them: the sum, 31, is its status;
//   - ifunc: a local IFUNC called directly, called through a pointer in .data and its address compared with the
//     pointer's (a stub's address everywhere), and a global one called through its GOT slot: 21 + 21 + 10 + 1 = 53.
//     As a static PIE too, as lld's link runs; GNU ld's gives the pointer in .data the IFUNC's implementation, from an
//     IRELATIVE relocation of its own, where a lea gives its .iplt entry, and the comparison fails: status 52;
//   - ifunc-calls: an IFUNC called directly, called through its GOT slot and loaded through it, which nothing else
//     takes the address of: its stub only calls reach, so its loads through the GOT read the stub's slot, which its
//     IRELATIVE relocation fills with its code, as lld's read its .got.plt entry: 21 * 3 = 63;
//   - weak-ifunc: a weak IFUNC defined nowhere, which has no resolver: loaded through its GOT slot, taken by a lea and
//     pointed to from .data, each 0, and called by a function that never runs: status 0. lld 23 links it wrong: it
//     makes an IRELATIVE relocation of the IFUNC with resolver 0, and its program faults at startup (GNU ld's runs). A
//     static PIE refuses its lea of address 0, as GNU ld does;
//   - absolute: absolute symbols, one at 42 and one below the image, taken by a lea, through a GOT slot, as a 32-bit
//     immediate and pointed to from .data, each its value: status 0. A static PIE refuses its lea of an absolute
//     symbol, as GNU ld and lld do.
static const StaticTestSource static_test_sources[] = {
    {
        .name = "priorities",
        .source = "#include <stdio.h>\n"
                  "static char order[32];\n"
                  "static int count;\n"
                  "static void note(char c) { order[count++] = c; }\n"
                  "__attribute__((constructor(300))) static void c300(void) { note('c'); }\n"
                  "__attribute__((constructor(101))) static void c101(void) { note('a'); }\n"
                  "__attribute__((constructor)) static void plain(void) { note('d'); }\n"
                  "__attribute__((constructor(200))) static void c200(void) { note('b'); }\n"
                  "__attribute__((destructor(150))) static void d150(void) { puts(\"destructor 150\"); }\n"
                  "__attribute__((destructor(250))) static void d250(void) { puts(\"destructor 250\"); }\n"
                  "__attribute__((destructor)) static void dplain(void) { puts(\"destructor plain\"); }\n"
                  "int main(void) { printf(\"%.*s %d\\n\", count, order, __builtin_cpu_supports(\"sse2\") != 0); return 0; }\n",
    },
    {
        .name = "decimal",
        .source = "#include <fenv.h>\n#include <pthread.h>\n#include <stdio.h>\n"
                  "extern void __dfp_set_round(int);\n"
                  "extern int __dfp_get_round(void);\n"
                  "extern void __dfp_clear_except(void);\n"
                  "extern int __dfp_test_except(int);\n"
                  "static volatile _Decimal64 one = 1.DD, three = 3.DD, a = 1.10DD, b = 3.00DD;\n"
                  "static void* upward(void* result) { __dfp_set_round(2); *(_Decimal64*)result = one / three; return 0; }\n"
                  "int main(void) {\n"
                  "    __dfp_clear_except();\n"
                  "    const int product = (int)(a * b * 100);\n"
                  "    const int exact = __dfp_test_except(FE_INEXACT) != 0;\n"
                  "    const _Decimal64 nearest = one / three;\n"
                  "    const int inexact = __dfp_test_except(FE_INEXACT) != 0;\n"
                  "    _Decimal64 up = 0.DD;\n"
                  "    pthread_t thread;\n"
                  "    pthread_create(&thread, 0, upward, &up);\n"
                  "    pthread_join(thread, 0);\n"
                  "    printf(\"%d %d %d %d %d\\n\", product, exact, inexact, __dfp_get_round(), up > nearest);\n"
                  "    return 0;\n"
                  "}\n",
        .option = "-pthread",
    },
    {
        .name = "nested",
        .source = "#include <stdio.h>\n"
                  "static int apply(int (*f)(int), int x) { return f(x); }\n"
                  "int main(void) {\n"
                  "    int offset = 3;\n"
                  "    int add(int x) { return x + offset; }\n"
                  "    printf(\"%d\\n\", apply(add, 4));\n"
                  "    return 0;\n"
                  "}\n",
        .option = "-ftrampoline-impl=heap",
        .condition = "defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 14",
    },
    {
        .name = "tls",
        .source = "    .section .tdata,\"awT\",@progbits\n"
                  "    .align 4\n"
                  "    .globl shared_value\n"
                  "shared_value:\n"
                  "    .long 7\n"
                  "value:\n"
                  "    .long 5\n"
                  "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    pushq %rbx\n"
                  "    data16 leaq shared_value@tlsgd(%rip), %rdi\n"
                  "    .value 0x6666\n"
                  "    rex64 call __tls_get_addr@PLT\n"
                  "    movl (%rax), %ebx\n"
                  "    data16 leaq shared_value@tlsgd(%rip), %rdi\n"
                  "    data16 rex64 call *__tls_get_addr@GOTPCREL(%rip)\n"
                  "    addl (%rax), %ebx\n"
                  "    leaq value@tlsld(%rip), %rdi\n"
                  "    call __tls_get_addr@PLT\n"
                  "    addl value@dtpoff(%rax), %ebx\n"
                  "    addl shared_value@dtpoff(%rax), %ebx\n"
                  "    leaq value@tlsld(%rip), %rdi\n"
                  "    call *__tls_get_addr@GOTPCREL(%rip)\n"
                  "    addl value@dtpoff(%rax), %ebx\n"
                  "    movl %ebx, %eax\n"
                  "    popq %rbx\n"
                  "    ret\n"
                  "    .size main, .-main\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .assembly = true,
    },
    {
        .name = "ifunc",
        .source = "    .text\n"
                  "    .type chosen, @gnu_indirect_function\n"
                  "chosen:\n"
                  "    leaq answer(%rip), %rax\n"
                  "    ret\n"
                  "answer:\n"
                  "    movl $21, %eax\n"
                  "    ret\n"
                  "    .globl picked\n"
                  "    .type picked, @gnu_indirect_function\n"
                  "picked:\n"
                  "    leaq other(%rip), %rax\n"
                  "    ret\n"
                  "other:\n"
                  "    movl $10, %eax\n"
                  "    ret\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    pushq %rbx\n"
                  "    call chosen\n"
                  "    movl %eax, %ebx\n"
                  "    call *pointer(%rip)\n"
                  "    addl %eax, %ebx\n"
                  "    movq picked@GOTPCREL(%rip), %rax\n"
                  "    call *%rax\n"
                  "    addl %eax, %ebx\n"
                  "    leaq chosen(%rip), %rax\n"
                  "    cmpq pointer(%rip), %rax\n"
                  "    sete %al\n"
                  "    movzbl %al, %eax\n"
                  "    addl %eax, %ebx\n"
                  "    movl %ebx, %eax\n"
                  "    popq %rbx\n"
                  "    ret\n"
                  "    .size main, .-main\n"
                  "    .data\n"
                  "    .align 8\n"
                  "pointer:\n"
                  "    .quad chosen\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .assembly = true,
        .pie_status = 53,
    },
    {
        .name = "ifunc-calls",
        .source = "    .text\n"
                  "    .globl sum\n"
                  "    .type sum, @gnu_indirect_function\n"
                  "sum:\n"
                  "    leaq add(%rip), %rax\n"
                  "    ret\n"
                  "add:\n"
                  "    movl $21, %eax\n"
                  "    ret\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    pushq %rbx\n"
                  "    call sum\n"
                  "    movl %eax, %ebx\n"
                  "    call *sum@GOTPCREL(%rip)\n"
                  "    addl %eax, %ebx\n"
                  "    movq sum@GOTPCREL(%rip), %rax\n"
                  "    call *%rax\n"
                  "    addl %eax, %ebx\n"
                  "    movl %ebx, %eax\n"
                  "    popq %rbx\n"
                  "    ret\n"
                  "    .size main, .-main\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .assembly = true,
    },
    {
        .name = "weak-ifunc",
        .source = "    .weak absent\n"
                  "    .type absent, @gnu_indirect_function\n"
                  "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    movq absent@GOTPCREL(%rip), %rax\n"
                  "    leaq absent(%rip), %rdx\n"
                  "    orq %rdx, %rax\n"
                  "    orq pointer(%rip), %rax\n"
                  "    setnz %al\n"
                  "    movzbl %al, %eax\n"
                  "    ret\n"
                  "    .size main, .-main\n"
                  "never:\n"
                  "    call absent\n"
                  "    ret\n"
                  "    .data\n"
                  "    .align 8\n"
                  "pointer:\n"
                  "    .quad absent\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .assembly = true,
        .lld_wrong = true,
        .pie_refused = true,
    },
    {
        .name = "absolute",
        .source = "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    leaq buster_answer(%rip), %rax\n"
                  "    subq $42, %rax\n"
                  "    movq buster_answer@GOTPCREL(%rip), %rdx\n"
                  "    subq $42, %rdx\n"
                  "    orq %rdx, %rax\n"
                  "    movl $buster_answer, %edx\n"
                  "    subq $42, %rdx\n"
                  "    orq %rdx, %rax\n"
                  "    movq pointer(%rip), %rdx\n"
                  "    subq $42, %rdx\n"
                  "    orq %rdx, %rax\n"
                  "    leaq buster_low(%rip), %rdx\n"
                  "    subq $0x1000, %rdx\n"
                  "    orq %rdx, %rax\n"
                  "    setnz %al\n"
                  "    movzbl %al, %eax\n"
                  "    ret\n"
                  "    .size main, .-main\n"
                  "    .data\n"
                  "    .align 8\n"
                  "pointer:\n"
                  "    .quad buster_answer\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .definitions = "    .globl buster_answer\n"
                       "    .set buster_answer, 42\n"
                       "    .globl buster_low\n"
                       "    .set buster_low, 0x1000\n"
                       "    .section .note.GNU-stack,\"\",@progbits\n",
        .assembly = true,
        .pie_refused = true,
    },
};

#define BUSTER_STATIC_TEST_SOURCES (BUSTER_ARRAY_LENGTH(bind_test_static_sources) + BUSTER_ARRAY_LENGTH(static_test_sources))

static StaticTestSource static_test_source(uint32_t index)
{
    const uint32_t bind_sources = BUSTER_ARRAY_LENGTH(bind_test_static_sources);
    StaticTestSource result = index < bind_sources ? (StaticTestSource){ 0 } : static_test_sources[index - bind_sources];
    if (index < bind_sources)
    {
        result.name = bind_test_static_sources[index].name;
        result.source = bind_test_static_sources[index].source;
        result.option = bind_test_static_sources[index].option;
    }
    return result;
}

// Writes `text` to the file at `path`: whether it could.
static bool static_test_write_text(const char* path, const char* text)
{
    FILE* file = fopen(path, "w");
    const bool result = file != 0 && fputs(text, file) >= 0;
    const bool closed = file != 0 && fclose(file) == 0;
    return result && closed;
}

// Assembles `definitions` into an object of its own beside the object at `object_path`, then combines the two with
// ld -r into `object_path`: whether it could. GNU as resolves a PC-relative reference to an absolute symbol that its
// file defines, so the absolute symbols such references reach come from another object, as a program's do when it
// takes them from another file or a linker script. The files it makes beside the object are removed.
static bool static_test_combine(const char* object_path, const char* definitions, const char* out, const char* err)
{
    char source_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES + 16];
    char definitions_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES + 16];
    char combined_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES + 16];
    snprintf(source_path, sizeof(source_path), "%s.defs.s", object_path);
    snprintf(definitions_path, sizeof(definitions_path), "%s.defs.o", object_path);
    snprintf(combined_path, sizeof(combined_path), "%s.combined", object_path);
    char* assemble[] = { "cc", "-c", "-o", definitions_path, source_path, 0 };
    char* combine[] = { "ld", "-r", "-o", combined_path, (char*)object_path, definitions_path, 0 };
    const bool result = static_test_write_text(source_path, definitions) && object_test_run(assemble, out, err) == 0 && object_test_run(combine, out, err) == 0 &&
                        rename(combined_path, object_path) == 0;
    unlink(source_path);
    unlink(definitions_path);
    unlink(combined_path);
    return result;
}

// Whether cc's preprocessor makes `condition` true, from a file at `path` that it writes and removes.
static bool static_test_condition(const char* path, const char* condition, const char* out, const char* err)
{
    char text[512];
    snprintf(text, sizeof(text), "#if !(%s)\n#error the condition doesn't hold\n#endif\n", condition);
    char* arguments[] = { "cc", "-E", "-o", "/dev/null", "-x", "c", (char*)path, 0 };
    const bool result = static_test_write_text(path, text) && object_test_run(arguments, out, err) == 0;
    unlink(path);
    return result;
}

// What the cross-check compares of two static executables, or two static PIEs (`pie`), of one program: their IRELATIVE
// and RELATIVE relocations (in any SHT_RELA section), whether they have a TLS segment and its alignment, and their init
// and fini arrays' sizes.
typedef struct StaticTestShape StaticTestShape;
struct StaticTestShape
{
    uint64_t irelatives;
    uint64_t relatives;
    uint64_t tls_alignment;
    uint64_t init_array;
    uint64_t fini_array;
    bool tls;
    bool valid;
};

static StaticTestShape static_test_shape(String8 file, bool pie)
{
    const ExecutableTestImage image = executable_test_image_of(file, pie ? ET_DYN : ET_EXEC);
    StaticTestShape result = {
        .valid = image.valid,
    };
    for (uint32_t i = 1; image.valid && i < image.header->e_shnum; i += 1)
    {
        const Elf64_Shdr* section = &image.section_headers[i];
        const bool relocations = section->sh_type == SHT_RELA && section->sh_offset + section->sh_size <= file.length;
        const Elf64_Rela* rela = relocations ? (const Elf64_Rela*)(file.pointer + section->sh_offset) : 0;
        for (uint64_t r = 0; relocations && r < section->sh_size / sizeof(Elf64_Rela); r += 1)
        {
            result.irelatives += ELF64_R_TYPE(rela[r].r_info) == R_X86_64_IRELATIVE ? 1 : 0;
            result.relatives += ELF64_R_TYPE(rela[r].r_info) == R_X86_64_RELATIVE ? 1 : 0;
        }
    }
    // .relr.dyn's relocations: an even word an address's, an odd one a bitmap of up to 63.
    const DynamicTestImage dynamic = dynamic_test_image(file);
    const Elf64_Shdr* relr = pie && dynamic.valid ? dynamic_test_section(&dynamic, ".relr.dyn") : 0;
    const uint8_t* relr_bytes = dynamic_test_bytes(&dynamic, relr);
    for (uint64_t w = 0; relr != 0 && w < relr->sh_size / 8; w += 1)
    {
        uint64_t word = 0;
        memcpy(&word, relr_bytes + 8 * w, sizeof(word));
        result.relatives += (word & 1) == 0 ? 1 : (uint64_t)__builtin_popcountll(word >> 1);
    }
    const Elf64_Phdr* tls = executable_test_segment(&image, PT_TLS);
    const Elf64_Shdr* init = executable_test_section(&image, ".init_array");
    const Elf64_Shdr* fini = executable_test_section(&image, ".fini_array");
    result.tls = tls != 0;
    result.tls_alignment = tls != 0 ? tls->p_align : 0;
    result.init_array = init != 0 ? init->sh_size : 0;
    result.fini_array = fini != 0 ? fini->sh_size : 0;
    return result;
}

// How many of a link's symbols only a test or a binop reads through the GOT (adc, add, and, cmp, or, sbb, sub and xor),
// with a REX prefix and the addend -4: lld writes those instructions with immediates in an executable that isn't a PIE
// (its R_RELAX_GOT_PC_NOPIC for R_X86_64_REX_GOTPCRELX, which GNU as writes for them), where the import gives them
// GOTPCREL and the writer keeps a slot. A symbol of the image's that isn't an IFUNC, read by no other fixup through the
// GOT unrelaxed.
static uint64_t static_test_immediate_slots(const Link* link, Arena* arena)
{
    const uint64_t length = arena->length;
    uint8_t* reads = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, link->symbols.count);
    uint64_t result = 0;
    if (reads != 0)
    {
        memset(reads, 0, link->symbols.count);
    }
    for (AtomId atom = 1; reads != 0 && atom < link->atoms.count; atom += 1)
    {
        const uint8_t* bytes = link->atoms.bytes[atom];
        const FixupId begin = link->atoms.fixup_begin[atom];
        for (FixupId fixup = begin; fixup < begin + link->atoms.fixup_count[atom]; fixup += 1)
        {
            const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const SymbolId target = link->fixups.target[fixup];
            const uint32_t offset = link->fixups.offset[fixup];
            const uint8_t opcode = offset >= 2 ? bytes[offset - 2] : 0;
            const bool rex = offset >= 3 && (bytes[offset - 3] & 0xf0) == 0x40;
            const bool immediate = kind == BUSTER_LINK_FIXUP_GOTPCREL && rex && (opcode == 0x85 || (opcode & 0xc7) == 0x03) && link_fixup_addend(link, fixup) == -4 &&
                                   executable_test_in_image(link, target) && !executable_test_is_ifunc(link, target);
            // 1: read by such an instruction, 2: through the GOT by another fixup that doesn't relax.
            reads[target] |= immediate ? 1 : static_test_reads_slot(kind) && !executable_test_relaxes(link, fixup) ? 2 : 0;
        }
    }
    for (SymbolId symbol = 1; reads != 0 && symbol < link->symbols.count; symbol += 1)
    {
        result += reads[symbol] == 1 ? 1 : 0;
    }
    arena_pop_to(arena, length);
    return result;
}

// How a program ran: whether it exited, its status, and what it wrote to standard output.
typedef struct StaticTestRun StaticTestRun;
struct StaticTestRun
{
    uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
    uint64_t length;
    int status;
    bool exited;
};

static StaticTestRun static_test_run(const char* path)
{
    StaticTestRun result = { 0 };
    int status = -1;
    const bool ran = executable_test_run(path, result.output, &result.length, &status);
    result.exited = ran && WIFEXITED(status);
    result.status = result.exited ? WEXITSTATUS(status) : -1;
    return result;
}

static bool static_test_same_runs(const StaticTestRun* a, const StaticTestRun* b)
{
    return a->exited && b->exited && a->status == b->status && a->length == b->length && memcmp(a->output, b->output, a->length) == 0;
}

// The link's symbol of a name, or 0.
static SymbolId static_test_symbol(const Link* link, const char* name)
{
    SymbolId result = 0;
    for (SymbolId symbol = 1; result == 0 && symbol < link->symbols.count; symbol += 1)
    {
        result = executable_named(link, symbol, name) ? symbol : 0;
    }
    return result;
}

// The link's first fixup of a kind, to a symbol named `target` if it isn't null, or 0.
static FixupId static_test_first_fixup(const Link* link, uint8_t kind, const char* target)
{
    FixupId result = 0;
    for (FixupId fixup = 1; result == 0 && fixup < link->fixups.count; fixup += 1)
    {
        const bool to = target == 0 || executable_named(link, link->fixups.target[fixup], target);
        result = (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) == kind && to ? fixup : 0;
    }
    return result;
}

// Writes the link BUSTER_STATIC_TEST_WRITES times as static_test_lanes does (every lane count, then the exact twins),
// each of which must fail with `error`, naming `symbol` if it isn't 0 and otherwise `fixup`, without an output: whether
// every one did.
static bool static_test_refuses(StaticTestTally* tally, Link* link, const Bind* bind, const Runtime* runtime, String8 path, const char* name, ExecutableError error,
                                SymbolId symbol, FixupId fixup)
{
    bool result = true;
    for (uint32_t w = 0; w < BUSTER_STATIC_TEST_WRITES; w += 1)
    {
        const uint32_t lanes = static_test_write_lanes(w);
        const bool exact = w >= BUSTER_ARRAY_LENGTH(static_test_lane_counts);
        DynamicTestWrite write = {
            .exact = exact,
        };
        ExecutableResult written;
        unlink(path.pointer);
        const bool succeeded = static_test_write_with(&write, link, bind, runtime, path, lanes, &written);
        const bool named = symbol != 0 ? written.symbol == symbol : written.fixup == fixup;
        const bool refused = !succeeded && written.error == error && named && access(path.pointer, F_OK) != 0;
        if (!refused)
        {
            executable_test_print_failure(name, &written);
        }
        static_test_expect(tally, refused, name, (exact ? 100 : 0) + lanes);
        result = result && refused;
    }
    return result;
}

// Each source compiled by cc, imported as the program's unit and bound by the vector path, then written by
// static_test_lanes into a/NAME and checked by static_test_structure: it must run as cc -static's link of the same
// object into b/NAME does, and with the cross-check as lld's into c/NAME does, which must have its shape
// (static_test_shape); as a static PIE, into d/NAME, e/NAME and f/NAME by cc -static-pie's links, but for one that runs
// otherwise there with GNU ld (StaticTestSource.pie_status), and one that a static PIE refuses
// (StaticTestSource.pie_refused), which every write must refuse, naming its first PC32, as cc -static-pie refuses it.
static bool static_test_compiled(const ExecutableTestDirectory* directory, const Runtime* runtime, bool cross_check)
{
    const bool pie = static_test_pie(runtime);
    char* const flag = pie ? "-static-pie" : "-static";
    StaticTestTally tally = {
        .check = pie ? "compiled, static PIEs" : "compiled",
    };
    char sides[3][BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    bool made = true;
    for (uint32_t side = 0; side < 3; side += 1)
    {
        snprintf(sides[side], sizeof(sides[side]), "%s/%c", directory->path, (pie ? 'd' : 'a') + side);
        made = made && mkdir(sides[side], 0700) == 0;
    }
    static_test_expect(&tally, made, "the links' directories made", 0);
    for (uint32_t s = 0; made && s < BUSTER_STATIC_TEST_SOURCES; s += 1)
    {
        const StaticTestSource program = static_test_source(s);
        char source_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char object_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char out[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char err[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char paths[3][2 * BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        snprintf(source_path, sizeof(source_path), "%s/%s.%s", directory->path, program.name, program.assembly ? "s" : "c");
        snprintf(object_path, sizeof(object_path), "%s/%s.o", directory->path, program.name);
        snprintf(out, sizeof(out), "%s/%s.cc.out", directory->path, program.name);
        snprintf(err, sizeof(err), "%s/%s.cc.err", directory->path, program.name);
        for (uint32_t side = 0; side < 3; side += 1)
        {
            snprintf(paths[side], sizeof(paths[side]), "%s/%s", sides[side], program.name);
        }
        const bool wanted = program.condition == 0 || static_test_condition(source_path, program.condition, out, err);
        char* compile[] = { "cc", "-O2", "-c", "-o", object_path, source_path, (char*)program.option, 0 };
        const bool compiled = wanted && static_test_write_text(source_path, program.source) && object_test_run(compile, out, err) == 0 &&
                              (program.definitions == 0 || static_test_combine(object_path, program.definitions, out, err));
        static_test_expect(&tally, !wanted || compiled, program.name, s);
        const bool refusing = pie && program.pie_refused;
        char* reference[] = { "cc", flag, "-o", paths[1], object_path, (char*)program.option, 0 };
        const bool referenced = compiled && object_test_run(reference, out, err) == 0;
        static_test_expect(&tally, !compiled || referenced != refusing, refusing ? "cc -static-pie refuses it" : pie ? "cc -static-pie links it" : "cc -static links it", s);
        Scratch scratch = scratch_begin(0);
        const String8 object = compiled ? object_test_read_file(scratch.arena, object_path) : (String8){ 0 };
        BindTestLink test = { 0 };
        const bool imported = object.pointer != 0 && bind_test_static_import(&test, runtime, object);
        static_test_expect(&tally, !compiled || imported, "the object imported", s);
        const bool bound = imported && bind_begin(&test.bind, &test.link, runtime, BIND_PATH_VECTOR, &test.bytes, 1, &test.arena) && bind_end(&test.bind, &test.link) &&
                           link_check(&test.link) == LINK_ERROR_NONE;
        static_test_expect(&tally, !imported || bound, "bound", test.bind.error);
        const String8 path = {
            .pointer = paths[0],
            .length = strlen(paths[0]),
        };
        ExecutableResult result = { 0 };
        String8 file = { 0 };
        const bool refused = bound && refusing &&
                             static_test_refuses(&tally, &test.link, &test.bind, runtime, path, program.name, EXECUTABLE_ERROR_RELOCATION, 0,
                                                 static_test_first_fixup(&test.link, BUSTER_LINK_FIXUP_PC32, 0));
        const bool written = bound && !refusing && static_test_lanes(&tally, &test.link, &test.bind, runtime, path, program.name, &result, &file, scratch.arena);
        const uint64_t relaxed = written ? static_test_structure(&tally, &test.link, &test.bind, runtime, file, &result, scratch.arena) : 0;
        const StaticTestRun ours = written ? static_test_run(paths[0]) : (StaticTestRun){ 0 };
        const StaticTestRun theirs = referenced ? static_test_run(paths[1]) : (StaticTestRun){ 0 };
        const bool own_status = pie && program.pie_status != 0;
        const bool runs_alike = own_status ? ours.exited && ours.status == program.pie_status : static_test_same_runs(&ours, &theirs);
        static_test_expect(&tally, !written || !referenced || runs_alike,
                           own_status ? "runs with its status, where GNU ld's static PIE differs" : pie ? "runs as cc -static-pie's link does" : "runs as cc -static's link does", s);
        // lld packs a static PIE's RELATIVE relocations as ours are when asked, so that their counts compare.
        ExecutableDynamic pie_dynamic = { 0 };
        ExecutableRuntime pie_inputs = { 0 };
        if (pie && bound)
        {
            dynamic_static_pie_inputs(&pie_dynamic, &pie_inputs, runtime, &test.bind);
        }
        char* const pack = pie_dynamic.relr ? "-Wl,-z,pack-relative-relocs" : "-Wl,-z,nopack-relative-relocs";
        char* lld[] = { "cc", flag, "-fuse-ld=lld", pack, "-o", paths[2], object_path, (char*)program.option, 0 };
        const bool lld_linked = cross_check && compiled && !refusing && object_test_run(lld, out, err) == 0;
        static_test_expect(&tally, !cross_check || !compiled || refusing || lld_linked, pie ? "lld links it with -static-pie" : "lld links it with -static", s);
        if (lld_linked && !program.lld_wrong)
        {
            const StaticTestRun lld_run = static_test_run(paths[2]);
            static_test_expect(&tally, !written || static_test_same_runs(&ours, &lld_run), "runs as lld's link does", s);
            const StaticTestShape shape = written ? static_test_shape(file, pie) : (StaticTestShape){ 0 };
            const StaticTestShape lld_shape = static_test_shape(object_test_read_file(scratch.arena, paths[2]), pie);
            static_test_expect(&tally, !written || (shape.valid && lld_shape.valid && shape.irelatives == lld_shape.irelatives), "as many IRELATIVE relocations as lld's",
                               shape.irelatives);
            static_test_expect(&tally, !written || !pie || shape.relatives == lld_shape.relatives, "as many RELATIVE relocations as lld's", shape.relatives);
            // The GOT's slots: lld's, its .got's and its IFUNCs' (.got.plt's that IRELATIVE relocations fill), against
            // ours but those of thread-locals' offsets, whose initial-exec accesses lld relaxes to local-exec, and in a
            // static executable those that only tests and binops read through the GOT, which lld writes with immediates
            // without PIC (static_test_immediate_slots). They must be as many: lld relaxes the same loads and calls, and
            // shares an IFUNC's slot with its stub alike.
            const ExecutableTestGot got = written ? executable_test_got(file, pie ? ET_DYN : ET_EXEC) : (ExecutableTestGot){ 0 };
            const ExecutableTestGot lld_got = executable_test_got(object_test_read_file(scratch.arena, paths[2]), pie ? ET_DYN : ET_EXEC);
            const uint64_t tls_slots = written ? executable_test_tls_slots(&test.link) : 0;
            const uint64_t immediates = written && !pie ? static_test_immediate_slots(&test.link, scratch.arena) : 0;
            const uint64_t slots = result.got_slots - tls_slots - immediates;
            static_test_expect(&tally, !written || (got.valid && lld_got.valid && slots == lld_got.slots),
                               pie ? "as many GOT slots as lld's, but for thread-locals' offsets" : "as many GOT slots as lld's, but for thread-locals' offsets and its immediates",
                               slots * 1000 + lld_got.slots);
            printf("  %s: %llu GOT slots (%llu of thread-locals' offsets, %llu that lld's immediates replace), lld's link %llu; %llu RELATIVE relocations, lld's link "
                   "%llu\n",
                   program.name, (unsigned long long)result.got_slots, (unsigned long long)tls_slots, (unsigned long long)immediates, (unsigned long long)lld_got.slots,
                   (unsigned long long)shape.relatives, (unsigned long long)lld_shape.relatives);
            static_test_expect(&tally, !written || (shape.tls == lld_shape.tls && shape.tls_alignment == lld_shape.tls_alignment), "lld's TLS segment's alignment",
                               shape.tls_alignment);
            static_test_expect(&tally, !written || (shape.init_array == lld_shape.init_array && shape.fini_array == lld_shape.fini_array), "lld's init and fini arrays' sizes",
                               shape.init_array);
        }
        uint32_t joined = 0;
        for (uint32_t m = 0; bound && m < runtime->member_count; m += 1)
        {
            joined += bind_test_has(test.bind.members_joined, m) ? 1 : 0;
        }
        if (!wanted)
        {
            printf("  %s: skipped, cc doesn't meet %s\n", program.name, program.condition);
        }
        else if (refusing)
        {
            printf("  %s: %s, as cc -static-pie refuses it\n", program.name, refused ? "refused" : "not refused");
        }
        else
        {
            printf("  %s: %u members joined, %u atoms, %u fixups, %llu stubs, %llu relocations (%llu RELATIVE), %llu loads and calls relaxed, %llu GOT slots, %llu bytes: "
                   "status %d (cc's %d), %s\n",
                   program.name, joined, bound ? test.link.atoms.count - 1 : 0, bound ? test.link.fixups.count - 1 : 0, (unsigned long long)result.stubs,
                   (unsigned long long)result.relocations, (unsigned long long)result.relative_relocations, (unsigned long long)relaxed, (unsigned long long)result.got_slots,
                   (unsigned long long)result.file_size, ours.status, theirs.status, written && runs_alike ? (own_status ? "its own status" : "the same output") : "different");
        }
        if (object.pointer != 0)
        {
            bind_test_link_end(&test);
        }
        scratch_end(scratch);
        for (uint32_t side = 0; !directory->keep && side < 3; side += 1)
        {
            unlink(paths[side]);
        }
        if (!directory->keep)
        {
            unlink(source_path);
            unlink(object_path);
            unlink(out);
            unlink(err);
        }
    }
    for (uint32_t side = 0; made && !directory->keep && side < 3; side += 1)
    {
        rmdir(sides[side]);
    }
    return static_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The refusals

// Assembly that cc assembles and that binding binds, which a static executable or a static PIE refuses: the error, and
// the symbol it names (by name) or the fixup (the first of its kind, or of its kind to `target`); the error a static
// PIE refuses it with, if another (`pie_error`), and whether only a static PIE refuses it; and assembly that defines
// absolute symbols in an object of its own (static_test_combine), or 0.
typedef struct StaticTestRefusal StaticTestRefusal;
struct StaticTestRefusal
{
    const char* name;
    const char* source;
    ExecutableError error;
    ExecutableError pie_error;
    bool pie_only;
    const char* symbol;
    uint8_t kind;
    const char* target;
    const char* definitions;
};

static const StaticTestRefusal static_test_refusals[] = {
    {
        .name = "a call of __tls_get_addr outside an access",
        .source = "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    subq $8, %rsp\n"
                  "    xorl %edi, %edi\n"
                  "    call __tls_get_addr@PLT\n"
                  "    addq $8, %rsp\n"
                  "    xorl %eax, %eax\n"
                  "    ret\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .error = EXECUTABLE_ERROR_UNSUPPORTED,
        .symbol = "__tls_get_addr",
    },
    {
        .name = "a general-dynamic access without its prefixes",
        .source = "    .section .tdata,\"awT\",@progbits\n"
                  "    .align 4\n"
                  "    .globl shared_value\n"
                  "shared_value:\n"
                  "    .long 7\n"
                  "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    subq $8, %rsp\n"
                  "    leaq shared_value@tlsgd(%rip), %rdi\n"
                  "    call __tls_get_addr@PLT\n"
                  "    movl (%rax), %eax\n"
                  "    addq $8, %rsp\n"
                  "    ret\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .error = EXECUTABLE_ERROR_FIXUP,
        .kind = BUSTER_LINK_FIXUP_TLSGD,
    },
    {
        .name = "a pointer to an absolute symbol farther than int32 from the base",
        .source = "    .globl far\n"
                  "    .set far, 0x123456789a\n"
                  "    .data\n"
                  "    .align 8\n"
                  "pointer:\n"
                  "    .quad far\n"
                  "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    movq pointer(%rip), %rax\n"
                  "    shrq $40, %rax\n"
                  "    ret\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .error = EXECUTABLE_ERROR_UNSUPPORTED,
        .symbol = "far",
    },
    {
        // Its offset from the base is INT32_MIN, which a reference holds, but S + A - P is below it from anywhere in
        // the image: GNU ld says the relocation is truncated to fit. A static PIE refuses a PC32 to any absolute symbol,
        // which no load address makes right.
        .name = "a lea of an absolute symbol farther than int32 from it",
        .source = "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    leaq far_below(%rip), %rax\n"
                  "    xorl %eax, %eax\n"
                  "    ret\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .error = EXECUTABLE_ERROR_FIXUP,
        .pie_error = EXECUTABLE_ERROR_RELOCATION,
        .kind = BUSTER_LINK_FIXUP_PC32,
        .target = "far_below",
        .definitions = "    .globl far_below\n"
                       "    .set far_below, 0xffffffff80400000\n"
                       "    .section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        // A text relocation: -z text, which cc -static-pie passes, refuses it.
        .name = "a pointer to main in .rodata",
        .source = "    .section .rodata\n"
                  "    .align 8\n"
                  "table:\n"
                  "    .quad main\n"
                  "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    movq table(%rip), %rax\n"
                  "    xorl %eax, %eax\n"
                  "    ret\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .error = EXECUTABLE_ERROR_RELOCATION,
        .pie_only = true,
        .kind = BUSTER_LINK_FIXUP_ABS64,
    },
    {
        // No load address fits a 32-bit absolute field of an address in the image.
        .name = "an ABS32 to main in .data",
        .source = "    .data\n"
                  "    .align 4\n"
                  "word:\n"
                  "    .long main\n"
                  "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    movl word(%rip), %eax\n"
                  "    xorl %eax, %eax\n"
                  "    ret\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .error = EXECUTABLE_ERROR_RELOCATION,
        .pie_only = true,
        .kind = BUSTER_LINK_FIXUP_ABS32,
    },
    {
        // Address 0, which a static PIE's weak reference to nothing is wherever the image loads: no field measured
        // from the image reaches it.
        .name = "a lea of a weak reference to nothing",
        .source = "    .weak absent\n"
                  "    .text\n"
                  "    .globl main\n"
                  "    .type main, @function\n"
                  "main:\n"
                  "    leaq absent(%rip), %rax\n"
                  "    xorl %eax, %eax\n"
                  "    ret\n"
                  "    .section .note.GNU-stack,\"\",@progbits\n",
        .error = EXECUTABLE_ERROR_RELOCATION,
        .pie_only = true,
        .kind = BUSTER_LINK_FIXUP_PC32,
        .target = "absent",
    },
};

// Assembles `source` into the directory as NAME.o, with `definitions` (if it isn't 0) combined into it
// (static_test_combine), then imports and binds it into `test` by the vector path: whether it could. `test` holds
// arenas whenever the object could be read, which `*imported` says.
static bool static_test_assemble(BindTestLink* test, const ExecutableTestDirectory* directory, const Runtime* runtime, const char* name, const char* source,
                                 const char* definitions, bool* imported, Arena* arena)
{
    char source_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char object_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char out[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char err[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snprintf(source_path, sizeof(source_path), "%s/%s.s", directory->path, name);
    snprintf(object_path, sizeof(object_path), "%s/%s.o", directory->path, name);
    snprintf(out, sizeof(out), "%s/%s.cc.out", directory->path, name);
    snprintf(err, sizeof(err), "%s/%s.cc.err", directory->path, name);
    char* assemble[] = { "cc", "-c", "-o", object_path, source_path, 0 };
    const bool assembled = static_test_write_text(source_path, source) && object_test_run(assemble, out, err) == 0 &&
                           (definitions == 0 || static_test_combine(object_path, definitions, out, err));
    const String8 object = assembled ? object_test_read_file(arena, object_path) : (String8){ 0 };
    *imported = object.pointer != 0;
    const bool result = object.pointer != 0 && bind_test_static_import(test, runtime, object) &&
                        bind_begin(&test->bind, &test->link, runtime, BIND_PATH_VECTOR, &test->bytes, 1, &test->arena) && bind_end(&test->bind, &test->link) &&
                        link_check(&test->link) == LINK_ERROR_NONE;
    if (!directory->keep)
    {
        unlink(source_path);
        unlink(object_path);
        unlink(out);
        unlink(err);
    }
    return result;
}

// The starved lane's room: at most this much, checked in steps of this much near the least it links with.
#define BUSTER_STATIC_TEST_ROOM_STEP 64
#define BUSTER_STATIC_TEST_MAX_ROOM (1ull << 20)

// A lane short of scratch memory (lane 0 of 1 and of 8 lanes, the last of 2 and of 8), writing the tls program: every
// write that fails must fail with EXECUTABLE_ERROR_MEMORY and leave no output, and one with room enough must run. As in
// test-dynamic, a bisection over multiples of 64 bytes finds about the least room, and the rooms tried are every KB up
// to it and every 64 bytes from 4 KB below it to 1 KB above. Every write starts from the atoms' bytes as binding left
// them, since the relaxation rewrites the accesses.
static void static_test_starved(StaticTestTally* tally, BindTestLink* test, const Runtime* runtime, String8 path, int status, Arena* arena)
{
    static const uint32_t starved[][2] = {
        { 1, 0 },
        { 8, 0 },
        { 2, 1 },
        { 8, 7 },
    };
    const uint8_t* saved = executable_test_save(&test->link, arena);
    static_test_expect(tally, saved != 0, "room for the atoms' bytes", 0);
    for (uint32_t c = 0; saved != 0 && c < BUSTER_ARRAY_LENGTH(starved); c += 1)
    {
        const uint32_t lanes = starved[c][0];
        DynamicTestWrite write = {
            .starve = true,
            .starved_lane = starved[c][1],
        };
        ExecutableResult result;
        uint64_t low = 0;
        uint64_t high = BUSTER_STATIC_TEST_MAX_ROOM / BUSTER_STATIC_TEST_ROOM_STEP;
        write.room = high * BUSTER_STATIC_TEST_ROOM_STEP;
        executable_test_restore(&test->link, saved);
        const bool enough = static_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &result);
        while (enough && low < high)
        {
            const uint64_t middle = low + (high - low) / 2;
            write.room = middle * BUSTER_STATIC_TEST_ROOM_STEP;
            unlink(path.pointer);
            executable_test_restore(&test->link, saved);
            const bool succeeded = static_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &result);
            low = succeeded ? low : middle + 1;
            high = succeeded ? middle : high;
        }
        const uint64_t least = high * BUSTER_STATIC_TEST_ROOM_STEP;
        uint64_t wrong = UINT64_MAX;
        for (uint64_t room = 0; enough && room < least + 1024; room += room + 4096 < least ? 1024 : BUSTER_STATIC_TEST_ROOM_STEP)
        {
            unlink(path.pointer);
            executable_test_restore(&test->link, saved);
            write.room = room;
            const bool succeeded = static_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &result);
            const bool clean = succeeded || (result.error == EXECUTABLE_ERROR_MEMORY && access(path.pointer, F_OK) != 0);
            wrong = !clean && wrong == UINT64_MAX ? room : wrong;
        }
        write.room = BUSTER_STATIC_TEST_MAX_ROOM;
        unlink(path.pointer);
        executable_test_restore(&test->link, saved);
        const bool succeeded = enough && static_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &result);
        const StaticTestRun run = succeeded ? static_test_run(path.pointer) : (StaticTestRun){ 0 };
        static_test_expect(tally, enough, "a write with a megabyte to spare", c);
        static_test_expect(tally, wrong == UINT64_MAX, "a lane short of memory fails the link with no output", wrong);
        static_test_expect(tally, succeeded && run.exited && run.status == status, "then succeeds with room enough", c);
        printf("  lane %u of %u starved: at least %llu bytes of room\n", starved[c][1], lanes, (unsigned long long)least);
    }
    unlink(path.pointer);
    executable_test_restore(&test->link, saved);
}

// Each refusal of the runtime's kind refused by every lane count and by the exact twins with 1 and 8 lanes, naming its
// symbol or fixup, without an output; then the starved lanes on the tls program, which runs with status 31.
static bool static_test_refused(const ExecutableTestDirectory* directory, const Runtime* runtime)
{
    const bool pie = static_test_pie(runtime);
    StaticTestTally tally = {
        .check = pie ? "refusals, static PIEs" : "refusals",
    };
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "refused", path_bytes);
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(static_test_refusals); r += 1)
    {
        const StaticTestRefusal* refusal = &static_test_refusals[r];
        const ExecutableError error = pie && refusal->pie_error != EXECUTABLE_ERROR_NONE ? refusal->pie_error : refusal->error;
        if (refusal->pie_only && !pie)
        {
            continue;
        }
        Scratch scratch = scratch_begin(0);
        BindTestLink test = { 0 };
        bool imported = false;
        char name[32];
        snprintf(name, sizeof(name), "refusal-%u", r);
        const bool bound = static_test_assemble(&test, directory, runtime, name, refusal->source, refusal->definitions, &imported, scratch.arena);
        static_test_expect(&tally, bound, refusal->name, r);
        const SymbolId symbol = bound && refusal->symbol != 0 ? static_test_symbol(&test.link, refusal->symbol) : 0;
        const FixupId fixup = bound && refusal->kind != 0 ? static_test_first_fixup(&test.link, refusal->kind, refusal->target) : 0;
        static_test_expect(&tally, !bound || symbol != 0 || fixup != 0, "what the refusal names is in the link", r);
        const bool outcome = bound && static_test_refuses(&tally, &test.link, &test.bind, runtime, path, refusal->name, error, symbol, fixup);
        printf("  %s: %s\n", refusal->name, outcome ? "refused" : "wrong");
        if (imported)
        {
            bind_test_link_end(&test);
        }
        scratch_end(scratch);
    }
    Scratch scratch = scratch_begin(0);
    BindTestLink test = { 0 };
    bool imported = false;
    uint32_t tls = 0;
    while (strcmp(static_test_sources[tls].name, "tls") != 0)
    {
        tls += 1;
    }
    const bool bound = static_test_assemble(&test, directory, runtime, "starved", static_test_sources[tls].source, 0, &imported, scratch.arena);
    static_test_expect(&tally, bound, "the tls program bound", 0);
    if (bound)
    {
        static_test_starved(&tally, &test, runtime, path, 31, scratch.arena);
    }
    if (imported)
    {
        bind_test_link_end(&test);
    }
    scratch_end(scratch);
    unlink(path.pointer);
    return static_test_report(&tally);
}

static int run_static_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t functions = BUSTER_EXECUTABLE_TEST_DEFAULT_FUNCTIONS;
    const char* parent = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    bool keep = false;
    bool cross_check = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &functions) && functions >= 1 && functions <= BUSTER_LINK_PROGRAM_MAX_COUNT;
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
        {
            parent = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else
        {
            valid = false;
        }
    }
    ExecutableTestDirectory directory = {
        .keep = keep,
    };
    const int length = snprintf(directory.path, sizeof(directory.path), "%s/buster-static-XXXXXX", parent);
    int result = 1;
    Runtime runtime;
    Runtime pie;
    const RuntimeError imported = valid ? runtime_import_static(&runtime) : RUNTIME_ERROR_NONE;
    const RuntimeError pie_imported = valid ? runtime_import_static_pie(&pie) : RUNTIME_ERROR_NONE;
    if (!valid)
    {
        fprintf(stderr, "usage: test-static [--seed N] [--functions N] [--directory PATH] [--cross-check] [--keep]\n");
    }
    else if (imported != RUNTIME_ERROR_NONE)
    {
        fprintf(stderr, "the static runtime didn't import: error %d, %s\n", (int)imported, runtime.failed_file);
    }
    else if (pie_imported != RUNTIME_ERROR_NONE)
    {
        fprintf(stderr, "the static PIE's runtime didn't import: error %d, %s\n", (int)pie_imported, pie.failed_file);
    }
    else if (length <= 0 || (uint64_t)length + 64 >= sizeof(directory.path) || mkdtemp(directory.path) == 0)
    {
        fprintf(stderr, "test-static: can't make a directory in %s\n", parent);
    }
    else if (!x86_encoder_initialized && !x86_encoder_initialize())
    {
        fprintf(stderr, "the encoder's templates can't be built\n");
    }
    else
    {
        bool passed = true;
        for (uint32_t kind = 0; kind < 2; kind += 1)
        {
            const Runtime* tested = kind == 0 ? &runtime : &pie;
            passed = static_test_programs(&directory, tested, seed, (uint32_t)functions) && passed;
            passed = static_test_compiled(&directory, tested, cross_check) && passed;
            passed = static_test_refused(&directory, tested) && passed;
        }
        result = passed ? 0 : 1;
        if (keep)
        {
            printf("kept %s\n", directory.path);
        }
        else
        {
            rmdir(directory.path);
        }
    }
    if (valid)
    {
        runtime_release(&runtime);
        runtime_release(&pie);
    }
    return result;
}
