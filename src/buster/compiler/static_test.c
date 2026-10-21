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
//     stub's address everywhere, its GOT slot too; and __rela_iplt_start and __rela_iplt_end at .rela.iplt's bounds;
//   - every fixup's field (static_test_expected): test-executable's kinds' values, with an IFUNC's stub for its address,
//     a general- or local-dynamic access as the local-exec sequence it relaxes to, DTPOFF32 as an offset from the
//     thread pointer, an absolute symbol's value, and nothing for a call of __tls_get_addr, which the access consumed;
//     and every name of the linker's at the place its section or segment says;
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
// Writing

// Writes a link bound against the static runtime to `path` as a static executable with a group of `lanes` lanes, from
// _start, with dynamic_static_inputs's inputs: whether executable_write succeeded, and its result. `write` holds the
// poison and starve options and whether the writer's exact twins write it.
static bool static_test_write_with(DynamicTestWrite* write, Link* link, const Bind* bind, const Runtime* runtime, String8 path, uint32_t lanes, ExecutableResult* result)
{
    ExecutableRuntime inputs;
    dynamic_static_inputs(&inputs, runtime, bind);
    write->link = link;
    write->options = (ExecutableOptions){
        .path = path,
        .entry = dynamic_global(runtime, bind, "_start"),
        .runtime = &inputs,
        .exact = write->exact,
    };
    write->succeeded = false;
    const bool ran = lane_run(lanes, dynamic_test_lane_main, write, 0);
    *result = write->result;
    return ran && write->succeeded;
}

// The atoms' bytes as binding left them, all in one copy on `arena`, or null if it has no room. A write patches them in
// place, and a TLS access's relaxation rewrites its sequence's bytes, which the next write would no longer find, so
// every write starts from this copy (static_test_restore).
static uint8_t* static_test_save(const Link* link, Arena* arena)
{
    uint64_t total = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        total += link->atoms.bytes[atom] != 0 ? link->atoms.size[atom] : 0;
    }
    uint8_t* result = arena_push(arena, total + 1, 64);
    uint64_t at = 0;
    for (AtomId atom = 1; result != 0 && atom < link->atoms.count; atom += 1)
    {
        const uint64_t size = link->atoms.bytes[atom] != 0 ? link->atoms.size[atom] : 0;
        memcpy(result + at, link->atoms.bytes[atom], size);
        at += size;
    }
    return result;
}

static void static_test_restore(Link* link, const uint8_t* saved)
{
    uint64_t at = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        const uint64_t size = link->atoms.bytes[atom] != 0 ? link->atoms.size[atom] : 0;
        memcpy(link->atoms.bytes[atom], saved + at, size);
        at += size;
    }
}

// The writes every link gets: one per lane count of static_test_lane_counts, then the exact twins' with 1 and 8 lanes.
#define BUSTER_STATIC_TEST_WRITES (BUSTER_ARRAY_LENGTH(static_test_lane_counts) + 2)

static uint32_t static_test_write_lanes(uint32_t write)
{
    const uint32_t counts = BUSTER_ARRAY_LENGTH(static_test_lane_counts);
    return write < counts ? static_test_lane_counts[write] : write == counts ? 1 : 8;
}

// Writes the link BUSTER_STATIC_TEST_WRITES times, each from its bytes as binding left them (static_test_save) with
// zeroed fields over the last file scribbled over, every other time over poisoned scratch memory: whether every write
// succeeded with the same bytes and build ID, which `file` then holds, read into `arena`, with the first write's result
// in `result`. The link is then as the last write left it, as every write leaves it.
static bool static_test_lanes(StaticTestTally* tally, Link* link, const Bind* bind, const Runtime* runtime, String8 path, const char* name, ExecutableResult* result,
                              String8* file, Arena* arena)
{
    bool same = true;
    *file = (String8){ 0 };
    const uint8_t* saved = static_test_save(link, arena);
    static_test_expect(tally, saved != 0, "room for the atoms' bytes", 0);
    for (uint32_t w = 0; saved != 0 && w < BUSTER_STATIC_TEST_WRITES; w += 1)
    {
        static_test_restore(link, saved);
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
    return same && saved != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The structure
//
// What a static executable with the C runtime says, read back from its headers and sections and compared with the link
// after the write (its atoms' addresses, its symbols' GOT slots and references) by rules of the test's own.

// The file's IFUNC tables: .rela.iplt's IRELATIVE relocations, .iplt's stubs and .got.
typedef struct StaticTestIfuncs StaticTestIfuncs;
struct StaticTestIfuncs
{
    const Elf64_Rela* relocations;
    uint64_t relocation_count;
    const uint8_t* stubs;
    uint64_t stubs_address;
    uint64_t stub_count;
    const uint8_t* got;
    uint64_t got_address;
    uint64_t got_size;
};

static StaticTestIfuncs static_test_ifuncs(const ExecutableTestImage* image, String8 file)
{
    const Elf64_Shdr* relocations = executable_test_section(image, ".rela.iplt");
    const Elf64_Shdr* stubs = executable_test_section(image, ".iplt");
    const Elf64_Shdr* got = executable_test_section(image, ".got");
    const bool relocations_inside = relocations != 0 && relocations->sh_offset + relocations->sh_size <= file.length;
    const bool stubs_inside = stubs != 0 && stubs->sh_offset + stubs->sh_size <= file.length;
    const bool got_inside = got != 0 && got->sh_offset + got->sh_size <= file.length;
    const StaticTestIfuncs result = {
        .relocations = relocations_inside ? (const Elf64_Rela*)(file.pointer + relocations->sh_offset) : 0,
        .relocation_count = relocations_inside ? relocations->sh_size / sizeof(Elf64_Rela) : 0,
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

static bool static_test_is_ifunc(const Link* link, SymbolId symbol)
{
    return link->symbols.atom[symbol] != 0 && (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_IFUNC;
}

// The IFUNCs that a fixup which doesn't read a GOT slot reaches, which have stubs, a byte per symbol in `stubbed`. The
// calls of __tls_get_addr (`tls_get_addr`) aren't such fixups: the TLS accesses consume them.
static void static_test_stubbed(const Link* link, SymbolId tls_get_addr, uint8_t* stubbed)
{
    memset(stubbed, 0, link->symbols.count);
    for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
    {
        const SymbolId target = link->fixups.target[fixup];
        const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        stubbed[target] |= !static_test_reads_slot(kind) && target != tls_get_addr && static_test_is_ifunc(link, target) ? 1 : 0;
    }
}

// The IFUNCs: each one with a stub at a stub's address, which jumps through a slot that an IRELATIVE relocation fills
// from the IFUNC's resolver, its GOT slot, if it has one, holding the stub's address; each one without a stub but with
// a GOT slot, an IRELATIVE relocation of the slot; and no relocation in .rela.iplt but those, each an IRELATIVE of a
// slot of .got. Returns how many IRELATIVE relocations there must be.
static uint64_t static_test_ifunc_checks(StaticTestTally* tally, const Link* link, const StaticTestIfuncs* ifuncs, const uint8_t* stubbed)
{
    uint64_t expected = 0;
    SymbolId wrong = 0;
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        if (static_test_is_ifunc(link, symbol))
        {
            const uint64_t resolver = BUSTER_EXECUTABLE_BASE + link->atoms.address[link->symbols.atom[symbol]] + link->symbols.value[symbol];
            const uint64_t stub = BUSTER_EXECUTABLE_BASE + link->symbols.reference[symbol];
            const uint32_t got_slot = link->symbols.got_slot[symbol];
            const uint64_t slot = ifuncs->got_address + 8 * (uint64_t)got_slot;
            uint64_t held = 0;
            if (got_slot != 0 && ifuncs->got != 0 && 8 * ((uint64_t)got_slot + 1) <= ifuncs->got_size)
            {
                memcpy(&held, ifuncs->got + 8 * (uint64_t)got_slot, sizeof(held));
            }
            const uint64_t stub_slot = stubbed[symbol] != 0 ? static_test_stub_slot(ifuncs, stub) : 0;
            const bool stub_right = stubbed[symbol] == 0 || (stub_slot != 0 && static_test_irelative(ifuncs, stub_slot) == resolver && (got_slot == 0 || held == stub));
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
    bool result = true;
    if (executable_named(link, symbol, "_GLOBAL_OFFSET_TABLE_"))
    {
        result = got != 0;
        *value = got != 0 ? got->sh_addr : 0;
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

// The value a fixup's target has in the file: an IFUNC with a stub its stub's address (its reference, which
// static_test_ifunc_checks checked), a name of the linker's static_test_linker_value's, an absolute symbol its value,
// and any other test-executable's (executable_test_symbol_value: a thread-local symbol its offset from the thread
// pointer). False if the test can't tell.
static bool static_test_symbol_value(const Link* link, const ExecutableTestImage* image, const uint8_t* stubbed, SymbolId symbol, uint64_t* value)
{
    const uint8_t source = link->symbols.source[symbol];
    bool result = true;
    if (stubbed[symbol] != 0)
    {
        *value = BUSTER_EXECUTABLE_BASE + link->symbols.reference[symbol];
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
        result = executable_test_symbol_value(link, image, symbol, value);
    }
    return result;
}

// The bytes fixup `fixup` of `holder` must leave there: `*length` bytes (up to 16) from `*start` in the atom. A general-
// or local-dynamic access leaves its local-exec sequence over its lea and its call (whose own fixup leaves nothing), and
// DTPOFF32 the offset from the thread pointer; the other kinds leave their field, as executable_test_expected works it
// out. False if the test can't tell, or a value can't fit: a link the tests write has none.
static bool static_test_expected(const Link* link, const ExecutableTestImage* image, const uint8_t* stubbed, SymbolId tls_get_addr, AtomId holder, FixupId fixup,
                                 uint8_t* expected, uint32_t* length, uint64_t* start)
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
    bool result = static_test_symbol_value(link, image, stubbed, target, &symbol);
    const uint64_t place = BUSTER_EXECUTABLE_BASE + link->atoms.address[holder] + offset;
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

// Every fixup's bytes in the file are static_test_expected's; and the GOT: a slot for each target of a GOT kind (but
// __tls_get_addr, whose calls the accesses consumed) and no other symbol, numbered from 1 in symbol order, each holding
// its symbol's value (an IFUNC's stub, a thread-local symbol's offset from the thread pointer) but an IFUNC's without a
// stub, which an IRELATIVE relocation fills; then one per stub, which static_test_ifunc_checks counted.
static void static_test_fields(StaticTestTally* tally, const Link* link, const ExecutableTestImage* image, String8 file, const uint8_t* stubbed, SymbolId tls_get_addr,
                               uint64_t stubs, Arena* arena)
{
    uint64_t mismatched = 0;
    FixupId first = 0;
    for (AtomId atom = 1; image->valid && atom < link->atoms.count; atom += 1)
    {
        const FixupId begin = link->atoms.fixup_begin[atom];
        const uint64_t atom_offset = executable_test_file_offset(image, BUSTER_EXECUTABLE_BASE + link->atoms.address[atom]);
        for (FixupId fixup = begin; fixup < begin + link->atoms.fixup_count[atom]; fixup += 1)
        {
            uint8_t expected[16];
            uint32_t length = 0;
            uint64_t start = 0;
            const bool known = static_test_expected(link, image, stubbed, tls_get_addr, atom, fixup, expected, &length, &start);
            const bool readable = atom_offset != UINT64_MAX && atom_offset + start + length <= file.length;
            const bool same = known && readable && memcmp(file.pointer + atom_offset + start, expected, length) == 0;
            first = !same && mismatched == 0 ? fixup : first;
            mismatched += same ? 0 : 1;
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
            targets[target] |= static_test_reads_slot(link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) && target != tls_get_addr ? 1 : 0;
        }
    }
    uint32_t next = 1;
    SymbolId wrong = 0;
    for (SymbolId symbol = 1; slots && image->valid && symbol < link->symbols.count; symbol += 1)
    {
        const uint32_t slot = link->symbols.got_slot[symbol];
        uint64_t expected = 0;
        uint64_t actual = 0;
        const bool known = static_test_symbol_value(link, image, stubbed, symbol, &expected);
        const uint64_t offset = got != 0 ? got->sh_offset + 8 * (uint64_t)slot : 0;
        const bool inside = got != 0 && 8 * ((uint64_t)slot + 1) <= got->sh_size && offset + 8 <= file.length;
        if (slot != 0 && inside)
        {
            memcpy(&actual, file.pointer + offset, sizeof(actual));
        }
        const bool filled = static_test_is_ifunc(link, symbol) && stubbed[symbol] == 0;
        const bool right = (slot != 0) == (targets[symbol] != 0) && (slot == 0 || (slot == next && inside && (filled || !known || actual == expected)));
        wrong = !right && wrong == 0 ? symbol : wrong;
        next += slot != 0 ? 1 : 0;
    }
    slots = slots && wrong == 0 && (got == 0 ? next == 1 && stubs == 0 : got->sh_size == 8 * ((uint64_t)next + stubs));
    static_test_expect(tally, slots, "a GOT slot for each GOT kind's target in symbol order, then one per stub", wrong);
    arena_pop_to(arena, length);
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

// The init and fini arrays: each class's atoms in the order GNU ld and lld put them, the prioritized ones by priority
// and in id order for equal ones, then the others in id order, one after another from the section's start to its end.
static void static_test_arrays(StaticTestTally* tally, const Link* link, const ExecutableTestImage* image, Arena* arena)
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
        uint64_t address = section != 0 ? section->sh_addr - BUSTER_EXECUTABLE_BASE : 0;
        bool ordered = entries != 0 && (count == 0 || section != 0);
        for (uint32_t i = 0; ordered && i < count; i += 1)
        {
            ordered = link->atoms.address[entries[i].atom] == address;
            address += link->atoms.size[entries[i].atom];
        }
        ordered = ordered && (count == 0 || address == section->sh_addr - BUSTER_EXECUTABLE_BASE + section->sh_size);
        static_test_expect(tally, ordered, array == 0 ? ".init_array's atoms by priority, then in id order" : ".fini_array's atoms by priority, then in id order", count);
        arena_pop_to(arena, length);
    }
}

// The headers: an executable that starts at _start (`entry`), without a program interpreter or .dynamic, with crt1.o's
// ABI note in a PT_NOTE, and PT_TLS over .tdata and .tbss.
static void static_test_headers(StaticTestTally* tally, const Link* link, const ExecutableTestImage* image, String8 file, const Runtime* runtime, SymbolId entry)
{
    const AtomId entry_atom = entry != 0 ? link->symbols.atom[entry] : 0;
    static_test_expect(tally, image->valid && entry_atom != 0 && image->header->e_entry == BUSTER_EXECUTABLE_BASE + link->atoms.address[entry_atom] + link->symbols.value[entry],
                       "an ET_EXEC that starts at _start", 0);
    static_test_expect(tally, executable_test_segment(image, PT_INTERP) == 0 && executable_test_segment(image, PT_DYNAMIC) == 0, "no PT_INTERP or PT_DYNAMIC", 0);
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
    static_test_expect(tally, note, "crt1.o's ABI note in a PT_NOTE", 0);
    const Elf64_Shdr* tdata = executable_test_section(image, ".tdata");
    const Elf64_Shdr* tbss = executable_test_section(image, ".tbss");
    const Elf64_Phdr* tls = executable_test_segment(image, PT_TLS);
    const uint64_t start = tdata != 0 ? tdata->sh_addr : tbss != 0 ? tbss->sh_addr : 0;
    const uint64_t end = tbss != 0 ? tbss->sh_addr + tbss->sh_size : tdata != 0 ? tdata->sh_addr + tdata->sh_size : 0;
    const bool tls_right = (tdata == 0 && tbss == 0) ? tls == 0
                                                     : tls != 0 && tls->p_vaddr == start && tls->p_memsz == end - start && tls->p_filesz == (tdata != 0 ? tdata->sh_size : 0);
    static_test_expect(tally, tls_right, "PT_TLS over .tdata and .tbss", 0);
}

// Every rule above, of a file `file` that executable_write (`result`) wrote of `link`, bound by `bind` against
// `runtime`.
static void static_test_structure(StaticTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, String8 file, const ExecutableResult* result,
                                  Arena* arena)
{
    const ExecutableTestImage image = executable_test_image(file);
    static_test_expect(tally, image.valid, "the file reads as an ET_EXEC", 0);
    ExecutableRuntime inputs;
    dynamic_static_inputs(&inputs, runtime, bind);
    const uint64_t length = arena->length;
    uint8_t* stubbed = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, link->symbols.count);
    static_test_expect(tally, stubbed != 0, "room for the IFUNCs' marks", 0);
    if (image.valid && stubbed != 0)
    {
        static_test_headers(tally, link, &image, file, runtime, dynamic_global(runtime, bind, "_start"));
        static_test_stubbed(link, inputs.tls_get_addr, stubbed);
        const StaticTestIfuncs ifuncs = static_test_ifuncs(&image, file);
        const uint64_t irelatives = static_test_ifunc_checks(tally, link, &ifuncs, stubbed);
        static_test_expect(tally, result->relocations == irelatives && result->stubs == ifuncs.stub_count, "the result's counts of IRELATIVE relocations and stubs",
                           result->relocations);
        static_test_fields(tally, link, &image, file, stubbed, inputs.tls_get_addr, ifuncs.stub_count, arena);
        // Each name of the linker's where its section or segment says, and __rela_iplt_start and __rela_iplt_end alike
        // without IRELATIVE relocations.
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
            wrong = known && BUSTER_EXECUTABLE_BASE + link->symbols.reference[symbol] != value && wrong == 0 ? symbol : wrong;
            iplt[0] = linker && executable_named(link, symbol, "__rela_iplt_start") ? link->symbols.reference[symbol] : iplt[0];
            iplt[1] = linker && executable_named(link, symbol, "__rela_iplt_end") ? link->symbols.reference[symbol] : iplt[1];
        }
        static_test_expect(tally, wrong == 0 && (ifuncs.relocation_count != 0 || iplt[0] == iplt[1]), "the linker's names where its sections and segments say", wrong);
        static_test_arrays(tally, link, &image, arena);
        const char* unwind = executable_test_unwind(link, file, BUSTER_EXECUTABLE_BASE, bind->unit_base, result);
        static_test_expect(tally, unwind == 0, unwind != 0 ? unwind : "the unwind tables", 0);
    }
    arena_pop_to(arena, length);
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs

// test-link's sets of options, and one more that calls libc with --functions workers.
#define BUSTER_STATIC_TEST_PROGRAMS (BUSTER_LINK_TEST_PROGRAMS + 1)

static bool static_test_programs(const ExecutableTestDirectory* directory, const Runtime* runtime, uint64_t seed, uint32_t functions)
{
    StaticTestTally tally = {
        .check = "programs",
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
            if (written)
            {
                static_test_structure(&tally, &bound.program.link, &bound.bind, runtime, file, &result, scratch.arena);
            }
            const bool ran = written && executable_test_runs(path.pointer, bound.program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES);
            static_test_expect(&tally, ran, "the program runs as its model predicts", i);
            printf("  %s: %u atoms, %u fixups, %llu stubs, %llu IRELATIVE relocations, %llu GOT slots, %llu FDEs, %llu bytes: %s\n", name,
                   bound.program.link.atoms.count - 1, bound.program.link.fixups.count - 1, (unsigned long long)result.stubs, (unsigned long long)result.relocations,
                   (unsigned long long)result.got_slots, (unsigned long long)result.fdes, (unsigned long long)result.file_size, ran ? "ran" : "didn't run");
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
// wrong, so that the cross-check only has lld link it.
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
//     pointer's (a stub's address everywhere), and a global one called through its GOT slot: 21 + 21 + 10 + 1 = 53;
//   - weak-ifunc: a weak IFUNC defined nowhere, which has no resolver: loaded through its GOT slot, taken by a lea and
//     pointed to from .data, each 0, and called by a function that never runs: status 0. lld 23 links it wrong: it
//     makes an IRELATIVE relocation of the IFUNC with resolver 0, and its program faults at startup (GNU ld's runs);
//   - absolute: absolute symbols, one at 42 and one below the image, taken by a lea, through a GOT slot, as a 32-bit
//     immediate and pointed to from .data, each its value: status 0.
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

// What the cross-check compares of two static executables of one program: their IRELATIVE relocations (in any
// SHT_RELA section), whether they have a TLS segment and its alignment, and their init and fini arrays' sizes.
typedef struct StaticTestShape StaticTestShape;
struct StaticTestShape
{
    uint64_t irelatives;
    uint64_t tls_alignment;
    uint64_t init_array;
    uint64_t fini_array;
    bool tls;
    bool valid;
};

static StaticTestShape static_test_shape(String8 file)
{
    const ExecutableTestImage image = executable_test_image(file);
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
        }
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

// Each source compiled by cc, imported as the program's unit and bound by the vector path, then written by
// static_test_lanes into a/NAME and checked by static_test_structure: it must run as cc -static's link of the same
// object into b/NAME does, and with the cross-check as lld's into c/NAME does, which must have its shape
// (static_test_shape).
static bool static_test_compiled(const ExecutableTestDirectory* directory, const Runtime* runtime, bool cross_check)
{
    StaticTestTally tally = {
        .check = "compiled",
    };
    char sides[3][BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    bool made = true;
    for (uint32_t side = 0; side < 3; side += 1)
    {
        snprintf(sides[side], sizeof(sides[side]), "%s/%c", directory->path, 'a' + side);
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
        char* reference[] = { "cc", "-static", "-o", paths[1], object_path, (char*)program.option, 0 };
        const bool referenced = compiled && object_test_run(reference, out, err) == 0;
        static_test_expect(&tally, !compiled || referenced, "cc -static links it", s);
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
        const bool written = bound && static_test_lanes(&tally, &test.link, &test.bind, runtime, path, program.name, &result, &file, scratch.arena);
        if (written)
        {
            static_test_structure(&tally, &test.link, &test.bind, runtime, file, &result, scratch.arena);
        }
        const StaticTestRun ours = written ? static_test_run(paths[0]) : (StaticTestRun){ 0 };
        const StaticTestRun theirs = referenced ? static_test_run(paths[1]) : (StaticTestRun){ 0 };
        static_test_expect(&tally, !written || !referenced || static_test_same_runs(&ours, &theirs), "runs as cc -static's link does", s);
        char* lld[] = { "cc", "-static", "-fuse-ld=lld", "-o", paths[2], object_path, (char*)program.option, 0 };
        const bool lld_linked = cross_check && compiled && object_test_run(lld, out, err) == 0;
        static_test_expect(&tally, !cross_check || !compiled || lld_linked, "lld links it with -static", s);
        if (lld_linked && !program.lld_wrong)
        {
            const StaticTestRun lld_run = static_test_run(paths[2]);
            static_test_expect(&tally, !written || static_test_same_runs(&ours, &lld_run), "runs as lld's link does", s);
            const StaticTestShape shape = written ? static_test_shape(file) : (StaticTestShape){ 0 };
            const StaticTestShape lld_shape = static_test_shape(object_test_read_file(scratch.arena, paths[2]));
            static_test_expect(&tally, !written || (shape.valid && lld_shape.valid && shape.irelatives == lld_shape.irelatives), "as many IRELATIVE relocations as lld's",
                               shape.irelatives);
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
        else
        {
            printf("  %s: %u members joined, %u atoms, %u fixups, %llu stubs, %llu IRELATIVE relocations, %llu bytes: status %d (cc's %d), %s\n", program.name, joined,
                   bound ? test.link.atoms.count - 1 : 0, bound ? test.link.fixups.count - 1 : 0, (unsigned long long)result.stubs,
                   (unsigned long long)result.relocations, (unsigned long long)result.file_size, ours.status, theirs.status,
                   written && static_test_same_runs(&ours, &theirs) ? "the same output" : "different");
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

// Assembly that cc assembles and that binding binds, which a static executable refuses: the error, and the symbol it
// names (by name) or the fixup (the first of its kind, or of its kind to `target`); and assembly that defines absolute
// symbols in an object of its own (static_test_combine), or 0.
typedef struct StaticTestRefusal StaticTestRefusal;
struct StaticTestRefusal
{
    const char* name;
    const char* source;
    ExecutableError error;
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
        // the image: GNU ld says the relocation is truncated to fit.
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
        .kind = BUSTER_LINK_FIXUP_PC32,
        .target = "far_below",
        .definitions = "    .globl far_below\n"
                       "    .set far_below, 0xffffffff80400000\n"
                       "    .section .note.GNU-stack,\"\",@progbits\n",
    },
};

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
    const uint8_t* saved = static_test_save(&test->link, arena);
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
        static_test_restore(&test->link, saved);
        const bool enough = static_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &result);
        while (enough && low < high)
        {
            const uint64_t middle = low + (high - low) / 2;
            write.room = middle * BUSTER_STATIC_TEST_ROOM_STEP;
            unlink(path.pointer);
            static_test_restore(&test->link, saved);
            const bool succeeded = static_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &result);
            low = succeeded ? low : middle + 1;
            high = succeeded ? middle : high;
        }
        const uint64_t least = high * BUSTER_STATIC_TEST_ROOM_STEP;
        uint64_t wrong = UINT64_MAX;
        for (uint64_t room = 0; enough && room < least + 1024; room += room + 4096 < least ? 1024 : BUSTER_STATIC_TEST_ROOM_STEP)
        {
            unlink(path.pointer);
            static_test_restore(&test->link, saved);
            write.room = room;
            const bool succeeded = static_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &result);
            const bool clean = succeeded || (result.error == EXECUTABLE_ERROR_MEMORY && access(path.pointer, F_OK) != 0);
            wrong = !clean && wrong == UINT64_MAX ? room : wrong;
        }
        write.room = BUSTER_STATIC_TEST_MAX_ROOM;
        unlink(path.pointer);
        static_test_restore(&test->link, saved);
        const bool succeeded = enough && static_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &result);
        const StaticTestRun run = succeeded ? static_test_run(path.pointer) : (StaticTestRun){ 0 };
        static_test_expect(tally, enough, "a write with a megabyte to spare", c);
        static_test_expect(tally, wrong == UINT64_MAX, "a lane short of memory fails the link with no output", wrong);
        static_test_expect(tally, succeeded && run.exited && run.status == status, "then succeeds with room enough", c);
        printf("  lane %u of %u starved: at least %llu bytes of room\n", starved[c][1], lanes, (unsigned long long)least);
    }
    unlink(path.pointer);
    static_test_restore(&test->link, saved);
}

// Each refusal refused by every lane count and by the exact twins with 1 and 8 lanes, naming its symbol or fixup,
// without an output; then the starved lanes on the tls program, which runs with status 31.
static bool static_test_refused(const ExecutableTestDirectory* directory, const Runtime* runtime)
{
    StaticTestTally tally = {
        .check = "refusals",
    };
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "refused", path_bytes);
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(static_test_refusals); r += 1)
    {
        const StaticTestRefusal* refusal = &static_test_refusals[r];
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
        bool outcome = bound;
        for (uint32_t w = 0; bound && w < BUSTER_STATIC_TEST_WRITES; w += 1)
        {
            const uint32_t lanes = static_test_write_lanes(w);
            const bool exact = w >= BUSTER_ARRAY_LENGTH(static_test_lane_counts);
            DynamicTestWrite write = {
                .exact = exact,
            };
            ExecutableResult result;
            unlink(path.pointer);
            const bool written = static_test_write_with(&write, &test.link, &test.bind, runtime, path, lanes, &result);
            const bool named = refusal->symbol != 0 ? result.symbol == symbol : result.fixup == fixup;
            const bool refused = !written && result.error == refusal->error && named && access(path.pointer, F_OK) != 0;
            if (!refused)
            {
                executable_test_print_failure(refusal->name, &result);
            }
            static_test_expect(&tally, refused, refusal->name, (exact ? 100 : 0) + lanes);
            outcome = outcome && refused;
        }
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
    const RuntimeError imported = valid ? runtime_import_static(&runtime) : RUNTIME_ERROR_NONE;
    if (!valid)
    {
        fprintf(stderr, "usage: test-static [--seed N] [--functions N] [--directory PATH] [--cross-check] [--keep]\n");
    }
    else if (imported != RUNTIME_ERROR_NONE)
    {
        fprintf(stderr, "the static runtime didn't import: error %d, %s\n", (int)imported, runtime.failed_file);
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
        bool passed = static_test_programs(&directory, &runtime, seed, (uint32_t)functions);
        passed = static_test_compiled(&directory, &runtime, cross_check) && passed;
        passed = static_test_refused(&directory, &runtime) && passed;
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
    }
    return result;
}
