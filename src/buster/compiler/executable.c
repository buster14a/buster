// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The direct path's executables: a link (link.c) laid out, patched and written as an executable, in one call that every
// lane of a group makes (docs/object-writer-and-linker.md, "The direct path (no -c)", "Layout", "Patching and writing",
// "The first executables", "The dynamic PIE", "The static executable with libc" and "The static PIE"). It writes four
// kinds:
//   - a static executable at a fixed address, without libc, crt objects or dynamic sections (step 4 of the plan): the
//     program's _start makes its system calls;
//   - the same with the C runtime (step 6), from a link that binding joined to the static runtime (crt1.o, libc.a's
//     members and the rest of the driver's group), with what ExecutableRuntime holds (dynamic.c's
//     dynamic_static_inputs): crt1.o's ABI note and the link's __tls_get_addr. Its IFUNCs' GOT slots and stubs get
//     IRELATIVE relocations in .rela.iplt, which libc.a's startup applies, and an IFUNC that a fixup reaches other than
//     through the GOT gets a stub in .iplt that stands for its address everywhere; its general- and local-dynamic TLS
//     accesses relax to local-exec (executable_relax_tls); its init and fini arrays' prioritized atoms go first, by
//     priority (executable_place_prioritized); and the linker defines the names libc.a's startup looks for besides
//     (executable_linker_names);
//   - a dynamic PIE against glibc (step 5), from a link that binding (bind.c) joined to the C runtime, with what
//     ExecutableDynamic holds (dynamic.c fills it): the program interpreter, libc.so.6 and its versions, Scrt1.o's ABI
//     note, DT_INIT's and DT_FINI's functions and the definitions to export;
//   - a static PIE (step 6), from a link that binding joined to the static PIE's runtime (rcrt1.o, crtbeginS.o and
//     crtendS.o with the static executable's archives), with both: an ExecutableDynamic without an interpreter, a
//     library, versions or exports, and the static executable's ExecutableRuntime (dynamic.c's
//     dynamic_static_pie_inputs). It is a PIE without dynamic symbols, which libc.a's startup relocates (its
//     _dl_relocate_static_pie applies .relr.dyn and .rela.dyn, its IFUNCs' IRELATIVE relocations last), with the static
//     executable's IFUNC stubs and TLS relaxation; and its loads and calls through the GOT of the image's symbols are
//     relaxed, as GNU ld and lld relax them, since rcrt1.o's _start calls through a slot before the startup has
//     relocated it (executable_relax_got).
// The linker defines __init_array_start, __init_array_end, __fini_array_start, __fini_array_end and
// _GLOBAL_OFFSET_TABLE_ for all four, and _DYNAMIC for the PIEs.
//
// The image, at BUSTER_EXECUTABLE_BASE (or a PIE's at 0, which the loader places), is lld's: four PT_LOAD segments in
// lld's order, each from the next page at its file offset's place in a page (4 KB-congruent, with contiguous file
// offsets and no file padding):
//   - R: the ELF header and program headers (which a program finds through AT_PHDR), a PIE's .interp, .note.ABI-tag
//     (with the C runtime), .note.gnu.build-id, a PIE's .dynsym, .gnu.version, .gnu.version_r, .gnu.hash, .dynstr,
//     .rela.dyn and .relr.dyn, a static executable's .rela.iplt (with the C runtime), .rodata (RODATA atoms, then
//     STRINGS atoms), .eh_frame_hdr and .eh_frame;
//   - RX: .text, .init and .fini (with the C runtime), and a PIE's stubs, .plt.got, or a static executable's, .iplt;
//   - RW, RELRO: .tdata and .tbss (the TLS image, PT_TLS), .init_array, .fini_array, .data.rel.ro, a PIE's .dynamic,
//     and .got, its memory size running to its page's end so that PT_GNU_RELRO covers it all;
//   - RW: .data and .bss.
// Then the section headers' string table and the section headers, which no segment loads. Within a section, atoms are
// ordered by alignment, largest first, and by id within an alignment, so that each one's offset is a prefix sum of the
// sizes before it with no padding: a data atom's size is padded to a multiple of its alignment, and a function's to 16
// bytes with int3 (functions are at least 16-byte aligned). The atoms of INIT_ARRAY and FINI_ARRAY, whose order the
// program runs, keep id order, but for the prioritized ones (link->priorities), which go first, by priority: they must
// be whole 8-byte entries, aligned to at most 8. So do the .eh_frame atoms and, with the C runtime, the .init and .fini
// atoms, the fragments of one piece each, which must land on their alignment where the one before ends (on 4 bytes for
// .eh_frame's records). Atoms aligned past a page aren't supported yet. The GOT has a slot for each target of CALL_GOT,
// LOAD_GOT, GOTPCREL and GOTTPOFF, and of a CALL to a PIE's dynamic symbol (its stub reads the slot), numbered from 1
// in symbol order (slot 0 stays zero, so that got_slot 0 means none), then one per stub of a static executable's: the
// target's address, its TLS offset for a TLS symbol, 0 for a dynamic symbol, which the loader fills, and for an IFUNC
// its stub's address, or 0 without one, which its IRELATIVE relocation fills. Nothing is relaxed but the static
// runtime's general- and local-dynamic TLS accesses, which call __tls_get_addr, which nothing there defines, and a
// static PIE's CALL_GOT and LOAD_GOT fixups with the addend -4 to the image's symbols but IFUNCs, whose targets then
// need no slot from them: on the direct path the code generator binds before it selects instructions, so the
// program's code reaches its own symbols directly, and only a static PIE needs the runtime's relaxed.
//
// A static executable needs no dynamic relocations but its IFUNCs' IRELATIVE ones, which libc.a's startup applies
// between __rela_iplt_start and __rela_iplt_end. A PIE's come from each target's relocation class
// (executable_relocation_class): RELATIVE for the image's symbols, to whose addresses the loader adds the load
// address; SYMBOLIC for the dynamic symbols (an import, or a weak reference to nothing with default visibility, which a
// library may define), .dynsym's undefined entries, which a static PIE has none of; NONE for a TLS symbol's offset, a
// weak reference to nothing that isn't dynamic and an absolute symbol's value. A GOT slot gets a RELATIVE relocation, or
// GLOB_DAT (TPOFF64 for a TLS import), or a static PIE's IFUNC's without a stub an IRELATIVE one; an ABS64 field of an
// atom the loader writes (TDATA, the init and fini arrays, DATA_RELRO and DATA), a RELATIVE relocation, or R_X86_64_64
// with the fixup's addend. With RELR (libc.so.6 defining GLIBC_ABI_DT_RELR, which the executable then needs, or a static
// PIE's libc.a relocating it with DT_RELR), the RELATIVE ones at even addresses are .relr.dyn's: each atom's encoded from
// its first, so that their words don't depend on the layout, and the GOT's a word of marks at a time. The others are
// .rela.dyn's, RELATIVE ones first, and a static PIE's IRELATIVE ones last. A CALL to a dynamic symbol goes to its stub
// in .plt.got (jmp [rip + slot], GNU ld's form). Refused (EXECUTABLE_ERROR_RELOCATION): an
// ABS64 that needs a relocation outside the atoms the loader writes (a text relocation), an ABS32 or ABS32S that needs
// one, and a fixup of another kind to a dynamic symbol, as lld refuses them; a PC32 or PC64 to address 0 (a weak
// reference to nothing that isn't dynamic), which no field measured from where the image loads reaches (GNU ld refuses
// that PC32 in code and read-only data; a PC32 in writable data and a PC64 anywhere, it and lld write off by the load
// address); and a CALL, PC32 or PC64 to an absolute symbol, for the same reason, as GNU ld and lld refuse them.
//
// A PIE's passes are data-parallel and vectorized, each with an exact twin, scalar, that ExecutableOptions.exact selects
// and the tests compare it with: the symbols' classes as bitsets, 64 symbols at a time (executable_mark_symbols); the
// fixups' targets tested 16 at a time against a filter of the symbols outside the image in two registers
// (executable_filter_test), whose few hits the exact check tells apart; the atoms the loader writes found 64 at a time
// (executable_written_atoms), and a dense atom's RELR words from its fixup count alone (executable_atom_is_dense); and
// the GOT's slots by class and their RELR words from the bitsets (executable_slots_of_class, executable_got_relr). So
// are both kinds' passes over atoms and symbols: the count's and the placement's blocks of 16 atoms of one bucket
// (executable_atom_block), whose padded sizes they sum and scan in vectors, and the symbols' references 16 at a time
// (executable_resolve), from gathers of their atoms' classes and addresses. And a static executable's with the C
// runtime: its IFUNCs as a bitset, 64 symbols' types at a time (and the atoms of those that have the type, 16 at a
// time), and a filter of them by two hashes that the count tests the fixups' targets against 16 at a time
// (executable_mark_ifuncs and executable_mark_ifunc_target), which in a static PIE has the symbols outside the image
// too, so that one test of each target serves both; the IFUNCs' slots that IRELATIVE relocations fill, from the bitsets
// (executable_ifunc_slots); and the TLS accesses and the calls of __tls_get_addr, counted by compares of 64 kinds and 16
// targets at a time. A static PIE's loads and calls through the GOT that may relax are found by compares of 64 kinds and
// addends, marked in a bitset of candidates whose relaxable symbols the reduction leaves out of the GOT 64 at a time
// (executable_unrelaxed), and relaxed one at a time in the patch's path for the other kinds. A TLS access's relaxation goes one
// access at a time in the patch's path for the kinds other than CALL and PC32, and lane 0 places the prioritized array
// atoms by a radix sort: both are few (docs/object-writer-and-linker.md, "The static executable with libc", has their
// costs).
//
// The columns it writes are the linker's: atoms' address (an image offset, the address less the base) and symbols'
// reference, got_slot and, in a PIE, dynamic_index (a dynamic symbol's or an export's .dynsym index, 0 for the others).
// A symbol's reference is its image offset, or a TLS symbol's offset from the thread pointer (variant II: from the end
// of the TLS block, which is the TLS segment's memory size rounded up to its alignment, since the segment starts on a
// multiple of its alignment), or for a weak symbol defined nowhere the image offset of address 0, or 0 if the symbol is
// thread-local, as GNU ld resolves one; in a PIE, a dynamic symbol's is its stub's image offset, or address 0's; and
// with the C runtime, an IFUNC's with a stub is its stub's image offset (an IFUNC is one with a resolver: a weak one
// defined nowhere is address 0, as any other is), and __tls_get_addr's 0, the relaxation consuming its calls; and an
// absolute symbol's is its value less the base (one farther than int32 from the base is refused). Every fixup's field is
// patched in place, in its atom's bytes.
//
// Patching and writing go a chunk at a time (BUSTER_EXECUTABLE_CHUNK_BYTES, about 256 KB, a quarter of Zen 5's L2), in
// chunks that never split a field or a TLS access's sequence (executable_fixup_extent): boundary k is at k chunks'
// bytes, unless an atom of at most a chunk holds it, which then ends there, or a larger atom's field or sequence does,
// which then ends there. The boundaries depend on the layout alone, not
// on the lanes. For each chunk, a lane patches the fields in it (executable_patch_pc32 an atom's whose fixups are all
// CALL and PC32, executable_patch_mixed another's, each fixup by its kind), hashes it while it is in L2 and writes it
// from there with pwritev. Every lane writes the chunks it patches: the writes take turns at the file's lock in the
// kernel, but each lane's overlaps the others' patching (docs/object-writer-and-linker.md, "Patching and writing", has
// what one lane writing them all costs instead). The build ID is a tree of the chunks' hashes: the hash of the chunks'
// hashes in chunk order, so that the lanes need nothing from each other to compute it and it doesn't depend on how many
// there are. Its note is hashed as zeros and written last.
//
// The file: overwritten in place (O_RDWR) when it exists as a regular file with one link that no process is running,
// then truncated to its size if that changed and given its mode; otherwise created, or written beside it and renamed
// over it (ETXTBSY: a process runs it; a file with other links, or a symbolic link). Never O_TRUNC, and closed before
// returning, since execve fails with ETXTBSY on a file open for writing. A link that fails after opening it removes it.
//
// Lanes: executable_write is one phase function, and every lane of the calling thread's group calls it with the same
// arguments (lane.c; one lane is the default). The atoms, fixups, symbols and GOT words are split once per lane
// (lane_range: each loop is a few nanoseconds per item), with count, place and fill by lane, and so are a PIE's words
// of the symbols' bitsets and its pieces of written atoms; the chunks are cut into parts by bytes and taken from the
// counter (lane_parts_by_cost and lane_take). Lane 0 writes the headers and a PIE's .dynamic, exports and .gnu.hash,
// places the prioritized array atoms and copies the ABI note with the C runtime, opens the output for every lane, and
// writes the build ID and closes it. The output is byte-identical for every lane count.
//
// main.c includes this file after lane.c, x86_64_encoder.c and link.c, and its String8. Layout, in this order:
//   - API: the base, the chunks, ExecutableSection, ExecutableError, ExecutableWrite, ExecutablePhase,
//     ExecutableDynamic, ExecutableRuntime, ExecutableOptions, ExecutableResult and executable_write's prototype.
//   - The chunk hash: ExecutableHash, executable_hash_begin, executable_hash_bytes and executable_hash_end.
//   - Patching: ExecutablePatch, executable_patch_pc32, the TLS relaxation (executable_relax_tls),
//     executable_patch_other, executable_patch_mixed and executable_patch_range.
//   - The file: executable_write_all, executable_open and executable_close.
//   - Layout: the buckets (executable_bucket), the blocks of atoms (executable_atom_block), ExecutableLayout and
//     executable_lay_out.
//   - Headers: executable_write_headers.
//   - Chunks: executable_chunk_boundary, executable_patch_chunk and executable_chunk_runs.
//   - Writing: ExecutableShared; the relocation classes (executable_relocation_class), the filter (ExecutableFilter)
//     and a PIE's symbols (executable_mark_symbols); a static executable's IFUNCs (executable_mark_ifuncs);
//     executable_count and executable_reduce_got; RELR (ExecutableRelr) and executable_classify;
//     executable_check_relocations; .dynamic and the totals (executable_dynamic_totals); executable_place,
//     executable_place_prioritized and executable_resolve; the relocations (executable_irelative_relocations,
//     executable_dynamic_relocations and executable_relr_relocations) and executable_dynamic_sections;
//     executable_chunks and executable_write.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <elf.h>

// OS-dependent includes
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <sys/uio.h>

// CPU architecture dependent includes
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

// Where the image starts: GNU ld's base for an executable that isn't a PIE. A dynamic PIE starts at 0, as GNU ld's and
// lld's do, and the loader places it.
#define BUSTER_EXECUTABLE_BASE 0x400000ull
#define BUSTER_EXECUTABLE_PAGE_BYTES 4096ull
#define BUSTER_EXECUTABLE_PAGE_LOG2 12
// The image's end, as an address: every image offset and PC-relative value fits in int32 below it.
#define BUSTER_EXECUTABLE_MAX_ADDRESS (1ull << 31)
// The largest atom, and the largest TLS block: every offset from the thread pointer, from minus the block's size up to
// 0, fits in int32. .tbss takes no address space, so the image's limit doesn't bound the block.
#define BUSTER_EXECUTABLE_MAX_ATOM_BYTES (1ull << 31)
#define BUSTER_EXECUTABLE_MAX_TLS_BYTES (1ull << 31)
// Functions start on 16 bytes and are padded with int3, as lld pads them.
#define BUSTER_EXECUTABLE_FUNCTION_ALIGNMENT_LOG2 4
#define BUSTER_EXECUTABLE_INT3 0xcc
// Patching, hashing and writing go a chunk at a time: a quarter of Zen 5's 1 MB L2.
#define BUSTER_EXECUTABLE_CHUNK_BYTES (256ull << 10)
// The build ID's hash, and its note: Elf64_Nhdr, "GNU" and its NUL, and the hash.
#define BUSTER_EXECUTABLE_BUILD_ID_BYTES 16
#define BUSTER_EXECUTABLE_NOTE_BYTES (sizeof(Elf64_Nhdr) + 4 + BUSTER_EXECUTABLE_BUILD_ID_BYTES)
// A dynamic PIE's stub for an import that calls reach: jmp [rip + its GOT slot] and a two-byte nop, as GNU ld's .plt.got
// entries are.
#define BUSTER_EXECUTABLE_STUB_BYTES 8

// The output sections, in the order of the file and of the addresses. .tbss takes no address space in its segment:
// the section after it starts where it does. Only a dynamic PIE has .interp, the ABI note, the dynamic symbols, their
// strings, versions and hash, the dynamic relocations, .init, .fini, the stubs and .dynamic; and only an executable
// whose link has unwind tables (EH_FRAME atoms) has .eh_frame and .eh_frame_hdr, its index.
typedef enum ExecutableSection
{
    EXECUTABLE_SECTION_INTERP,
    EXECUTABLE_SECTION_ABI_NOTE,
    EXECUTABLE_SECTION_BUILD_ID,
    EXECUTABLE_SECTION_DYNSYM,
    EXECUTABLE_SECTION_VERSYM,
    EXECUTABLE_SECTION_VERNEED,
    EXECUTABLE_SECTION_GNU_HASH,
    EXECUTABLE_SECTION_DYNSTR,
    EXECUTABLE_SECTION_RELA_DYN,
    EXECUTABLE_SECTION_RELR_DYN,
    EXECUTABLE_SECTION_RELA_IPLT,
    EXECUTABLE_SECTION_RODATA,
    EXECUTABLE_SECTION_EH_FRAME_HDR,
    EXECUTABLE_SECTION_EH_FRAME,
    EXECUTABLE_SECTION_TEXT,
    EXECUTABLE_SECTION_INIT,
    EXECUTABLE_SECTION_FINI,
    EXECUTABLE_SECTION_PLT_GOT,
    EXECUTABLE_SECTION_TDATA,
    EXECUTABLE_SECTION_TBSS,
    EXECUTABLE_SECTION_INIT_ARRAY,
    EXECUTABLE_SECTION_FINI_ARRAY,
    EXECUTABLE_SECTION_DATA_RELRO,
    EXECUTABLE_SECTION_DYNAMIC,
    EXECUTABLE_SECTION_GOT,
    EXECUTABLE_SECTION_DATA,
    EXECUTABLE_SECTION_BSS,
    EXECUTABLE_SECTION_COUNT,
} ExecutableSection;

typedef enum ExecutableError
{
    EXECUTABLE_ERROR_NONE,
    // Scratch memory ran out.
    EXECUTABLE_ERROR_MEMORY,
    // The image doesn't fit below BUSTER_EXECUTABLE_MAX_ADDRESS or the TLS block in BUSTER_EXECUTABLE_MAX_TLS_BYTES, or
    // an atom is larger than BUSTER_EXECUTABLE_MAX_ATOM_BYTES or aligned past a page, an INIT_ARRAY or FINI_ARRAY atom
    // isn't whole 8-byte entries aligned to at most 8, an atom of an ordered class (EH_FRAME, and a dynamic PIE's INIT
    // and FINI) doesn't land on its alignment where the one before it ends, or in a static executable an atom is of one
    // of the C runtime's classes (INIT and FINI), which an executable without libc has no section for
    // (ExecutableResult.atom).
    EXECUTABLE_ERROR_LAYOUT,
    // A symbol defined nowhere that isn't weak (ExecutableResult.symbol).
    EXECUTABLE_ERROR_UNDEFINED,
    // In a static executable, a symbol of a shared library; one the linker should define that it doesn't know; or with
    // the C runtime, __tls_get_addr called other than by as many general- and local-dynamic TLS accesses as there are
    // calls (ExecutableResult.symbol).
    EXECUTABLE_ERROR_UNSUPPORTED,
    // The entry point isn't a symbol defined in a TEXT atom.
    EXECUTABLE_ERROR_ENTRY,
    // A dynamic PIE can't relocate the fixup: an ABS64 that needs a relocation in an atom the loader doesn't write (a
    // text relocation), an ABS32 or ABS32S that needs one (no load address fits it), a fixup to a dynamic symbol of
    // another kind than a GOT kind, ABS64 and CALL, a PC32 or PC64 to address 0, or a CALL, PC32 or PC64 to an absolute
    // symbol (ExecutableResult.fixup).
    EXECUTABLE_ERROR_RELOCATION,
    // A fixup's value doesn't fit its field (ExecutableResult.fixup).
    EXECUTABLE_ERROR_FIXUP,
    // A system call on the output failed (ExecutableResult.call and system_error).
    EXECUTABLE_ERROR_FILE,
    // An EH_FRAME atom's records aren't ones .eh_frame_hdr can index, or an FDE's function is farther from the header
    // than its table's int32 reaches (see "Unwind tables"; ExecutableResult.atom).
    EXECUTABLE_ERROR_UNWIND,
} ExecutableError;

// How the output was written.
typedef enum ExecutableWrite
{
    EXECUTABLE_WRITE_NONE,
    // It existed, as a regular file of one link that no process was running: overwritten.
    EXECUTABLE_WRITE_IN_PLACE,
    // It didn't exist: created.
    EXECUTABLE_WRITE_CREATED,
    // It was running, had other links, was someone else's or was a symbolic link: written beside it and renamed over it.
    EXECUTABLE_WRITE_RENAMED,
    // It is a file of another type, such as /dev/null or a FIFO: written into, in order, and never replaced or removed.
    EXECUTABLE_WRITE_SEQUENTIAL,
} ExecutableWrite;

// executable_write's phases, whose ends lane 0 timestamps for the benchmark. Each has a row of the lanes' errors.
typedef enum ExecutablePhase
{
    // Counting the atoms per bucket, the GOT's targets and the FDEs, and a dynamic PIE's symbols, stubs and relocations
    // by class.
    EXECUTABLE_PHASE_COUNT,
    // Laying out the sections and the atoms, and numbering the GOT slots and the stubs.
    EXECUTABLE_PHASE_LAYOUT,
    // The symbols' references, the GOT's slots, the chunks' boundaries and the headers, and a dynamic PIE's dynamic
    // symbols and stubs.
    EXECUTABLE_PHASE_SYMBOLS,
    // .eh_frame_hdr's table, and a dynamic PIE's relocations and the rest of its dynamic sections.
    EXECUTABLE_PHASE_DYNAMIC,
    // Sorting .eh_frame_hdr's table if it must be, and opening the output.
    EXECUTABLE_PHASE_OPEN,
    // Patching, hashing and writing the chunks.
    EXECUTABLE_PHASE_CHUNKS,
    // The build ID, and closing.
    EXECUTABLE_PHASE_FINISH,
    EXECUTABLE_PHASES,
} ExecutablePhase;

// What a dynamic PIE has that a static executable doesn't, from binding (bind.c) and the C runtime (runtime.c), as
// plain data: the program interpreter, the libraries that the imports come from (libc.so.6, and the loader that
// libc.so's script names as needed) with their versions, a note to repeat (Scrt1.o's .note.ABI-tag), DT_INIT's and
// DT_FINI's functions (crti.o's _init and _fini) and the definitions to export (Bind.exports). The imports are the
// link's symbols of source RUNTIME_SHARED, each at the version its column holds (an ExecutableVersion's index, or 0 for
// none).
typedef struct ExecutableVersion ExecutableVersion;
struct ExecutableVersion
{
    String8 name;
    // The name's ELF hash: the library's vd_hash, which the executable's vna_hash repeats.
    uint32_t hash;
    // Its index among the libraries' version definitions, which runtime.c numbers apart (the loader's after
    // libc.so.6's).
    uint16_t index;
    // The library it is a version of: its place in ExecutableDynamic.needed.
    uint8_t library;
};

// The most libraries an executable needs: libc.so.6 and the loader.
#define BUSTER_EXECUTABLE_LIBRARIES 2

typedef struct ExecutableDynamic ExecutableDynamic;
struct ExecutableDynamic
{
    String8 interpreter;
    // The libraries the imports come from, in DT_NEEDED's order, empty for none: the first is needed whenever it is
    // named, and each other only when one of its versions is (libc.so's AS_NEEDED, as GNU ld and lld take it), which an
    // import from it is: glibc's exports all have versions. `versions` has each library's together, in this order.
    String8 needed[BUSTER_EXECUTABLE_LIBRARIES];
    const ExecutableVersion* versions;
    uint32_t version_count;
    String8 note;
    SymbolId init;
    SymbolId fini;
    const SymbolId* exports;
    uint32_t export_count;
    // The library's index of GLIBC_ABI_DT_RELR, whose definition says that its loader takes DT_RELR, or 0: with it,
    // the executable needs the version.
    uint16_t relr_version;
    // Whether the executable's RELATIVE relocations at even addresses are RELR's: a dynamic PIE's with
    // GLIBC_ABI_DT_RELR, and a static PIE's when libc.a relocates it with DT_RELR (dynamic.c).
    bool relr;
};

// What a static executable that links the C runtime (crt1.o, libc.a's members and the rest of the driver's group, which
// binding joined) has that one without it doesn't, as plain data: a note to repeat (crt1.o's .note.ABI-tag). Its .init
// and .fini come from the crt objects' fragments, its IFUNCs from their symbols' type, and the names its linker defines
// from their symbols' source.
typedef struct ExecutableRuntime ExecutableRuntime;
struct ExecutableRuntime
{
    String8 note;
    // The link's __tls_get_addr, which nothing in a static executable defines (bind.c leaves it undefined), or 0: each
    // call of it must be a general- or local-dynamic TLS access's, which relaxes to local-exec without the call.
    SymbolId tls_get_addr;
};

typedef struct ExecutableOptions ExecutableOptions;
struct ExecutableOptions
{
    // The output's path, which needn't be followed by a NUL.
    String8 path;
    // The entry point: a symbol defined in a TEXT atom (_start).
    SymbolId entry;
    // For a PIE, what it has besides; 0 for a static executable.
    const ExecutableDynamic* dynamic;
    // For the static runtime, what it has besides: a static executable's that links the C runtime, or with `dynamic`
    // a static PIE's (dynamic.c's dynamic_static_pie_inputs fills both); 0 for one without, and for a dynamic PIE.
    const ExecutableRuntime* runtime;
    // For the tests: a dynamic PIE's passes by their exact twins, scalar, which must write the same file.
    bool exact;
    // For the tests, or null: room for `boundary_capacity` of the chunks' boundaries, which lane 0 copies there (as many
    // as fit, of ExecutableResult.chunks + 1).
    uint64_t* boundaries;
    uint64_t boundary_capacity;
};

typedef struct ExecutableResult ExecutableResult;
struct ExecutableResult
{
    ExecutableError error;
    // What the error is about, when it is about one of them.
    AtomId atom;
    SymbolId symbol;
    FixupId fixup;
    // For EXECUTABLE_ERROR_FILE: the call that failed, and its errno.
    const char* call;
    int system_error;
    ExecutableWrite write;
    uint64_t file_size;
    uint64_t image_size;
    uint64_t chunks;
    uint64_t got_slots;
    // A dynamic PIE's dynamic symbols (but the null one), .rela.dyn's relocations (RELATIVE ones among them), the
    // RELATIVE relocations that .relr.dyn holds and its words, and the stubs.
    uint64_t dynamic_symbols;
    uint64_t relocations;
    uint64_t relative_relocations;
    uint64_t relr_relocations;
    uint64_t relr_words;
    uint64_t stubs;
    // The FDEs that .eh_frame_hdr's table indexes, and whether their entries came out sorted in .eh_frame's order, so
    // that the table didn't need its sort.
    uint64_t fdes;
    bool fdes_in_order;
    uint8_t build_id[BUSTER_EXECUTABLE_BUILD_ID_BYTES];
    // CLOCK_MONOTONIC in nanoseconds when executable_write started, and when each phase ended, on lane 0.
    uint64_t start_time;
    uint64_t phase_times[EXECUTABLE_PHASES];
};

// Lays out the link, fills its linker columns, patches its atoms' bytes and writes the executable to options->path.
// Every lane of the calling thread's group calls it, with the same arguments, and it returns once the file is written
// and closed, with the same value on every lane: whether it succeeded. Lane 0 fills *result; read it after the call.
// The link must have passed link_check, and must not change while it runs.
static __attribute__((unused)) bool executable_write(Link* link, const ExecutableOptions* options, ExecutableResult* result);

static uint64_t executable_align(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint64_t executable_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// ---------------------------------------------------------------------------------------------------------------------
// The chunk hash
//
// 128 bits of a byte stream that arrives in pieces: the same however the stream is cut. BUSTER_EXECUTABLE_HASH_LANES
// ZMM accumulators take the stream's 64-byte blocks in turn, block i going to accumulator i % 8, each block as the round
// key of one AES round of its accumulator and then one more round with a constant key; a partial last block is padded
// with zeros. Then the accumulators are folded into one with AES rounds in order, the length goes in, and its four
// 128-bit lanes are folded into one. On the 9700X it hashes a 256 KB chunk in L2 at 257 GB/s, as fast as reading it
// (docs/object-writer-and-linker.md, "Patching and writing"). It isn't a cryptographic hash: a build ID only has to
// tell builds apart.

#define BUSTER_EXECUTABLE_HASH_LANES 8
#define BUSTER_EXECUTABLE_HASH_BLOCK_BYTES 64
#define BUSTER_EXECUTABLE_HASH_STRIDE_BYTES (BUSTER_EXECUTABLE_HASH_LANES * BUSTER_EXECUTABLE_HASH_BLOCK_BYTES)

typedef struct ExecutableHash ExecutableHash;
struct ExecutableHash
{
    __m512i accumulators[BUSTER_EXECUTABLE_HASH_LANES];
    // The stream's bytes after its last whole block.
    uint8_t carry[BUSTER_EXECUTABLE_HASH_BLOCK_BYTES];
    uint64_t length;
};

// The first 48 outputs of splitmix64 from state 0: the accumulators' starting values (qwords 0 to 31), the round key
// (32 to 39) and the length's key (40 to 47).
static const uint64_t executable_hash_constants[48] __attribute__((aligned(64))) = {
    0xe220a8397b1dcdafull, 0x6e789e6aa1b965f4ull, 0x06c45d188009454full, 0xf88bb8a8724c81ecull,
    0x1b39896a51a8749bull, 0x53cb9f0c747ea2eaull, 0x2c829abe1f4532e1ull, 0xc584133ac916ab3cull,
    0x3ee5789041c98ac3ull, 0xf3b8488c368cb0a6ull, 0x657eecdd3cb13d09ull, 0xc2d326e0055bdef6ull,
    0x8621a03fe0bbdb7bull, 0x8e1f7555983aa92full, 0xb54e0f1600cc4d19ull, 0x84bb3f97971d80abull,
    0x7d29825c75521255ull, 0xc3cf17102b7f7f86ull, 0x3466e9a083914f64ull, 0xd81a8d2b5a4485acull,
    0xdb01602b100b9ed7ull, 0xa9038a921825f10dull, 0xedf5f1d90dca2f6aull, 0x54496ad67bd2634cull,
    0xdd7c01d4f5407269ull, 0x935e82f1db4c4f7bull, 0x69b82ebc92233300ull, 0x40d29eb57de1d510ull,
    0xa2f09dabb45c6316ull, 0xee521d7a0f4d3872ull, 0xf16952ee72f3454full, 0x377d35dea8e40225ull,
    0x0c7de8064963bab0ull, 0x05582d37111ac529ull, 0xd254741f599dc6f7ull, 0x69630f7593d108c3ull,
    0x417ef96181daa383ull, 0x3c3c41a3b43343a1ull, 0x6e19905dcbe531dfull, 0x4fa9fa7324851729ull,
    0x84eb4454a792922aull, 0x134f7096918175ceull, 0x07dc930b302278a8ull, 0x12c015a97019e937ull,
    0xcc06c31652ebf438ull, 0xecee65630a691e37ull, 0x3e84ecb1763e79adull, 0x690ed476743aae49ull,
};

static void executable_hash_begin(ExecutableHash* hash)
{
    for (uint32_t i = 0; i < BUSTER_EXECUTABLE_HASH_LANES; i += 1)
    {
        const __m512i start = _mm512_load_si512(executable_hash_constants + 8 * (i % 4));
        hash->accumulators[i] = _mm512_xor_si512(start, _mm512_set1_epi64((long long)(i + 1)));
    }
    hash->length = 0;
}

static __m512i executable_hash_key(void)
{
    return _mm512_load_si512(executable_hash_constants + 32);
}

static __m512i executable_hash_round(__m512i accumulator, __m512i block, __m512i key)
{
    return _mm512_aesenc_epi128(_mm512_aesenc_epi128(accumulator, block), key);
}

// Whole strides of 8 blocks from `bytes`, the first going to accumulator 0: the stream is at a stride's start. The
// accumulators stay in registers.
static void executable_hash_strides(ExecutableHash* hash, const uint8_t* bytes, uint64_t strides)
{
    const __m512i key = executable_hash_key();
    __m512i a0 = hash->accumulators[0];
    __m512i a1 = hash->accumulators[1];
    __m512i a2 = hash->accumulators[2];
    __m512i a3 = hash->accumulators[3];
    __m512i a4 = hash->accumulators[4];
    __m512i a5 = hash->accumulators[5];
    __m512i a6 = hash->accumulators[6];
    __m512i a7 = hash->accumulators[7];
    // Clang unrolls this loop four times otherwise, which on the 9700X hashes 256 KB in L2 at 176 GB/s rather than 257.
#pragma clang loop unroll(disable)
    for (uint64_t i = 0; i < strides; i += 1)
    {
        const uint8_t* stride = bytes + i * BUSTER_EXECUTABLE_HASH_STRIDE_BYTES;
        a0 = executable_hash_round(a0, _mm512_loadu_si512(stride + 0 * BUSTER_EXECUTABLE_HASH_BLOCK_BYTES), key);
        a1 = executable_hash_round(a1, _mm512_loadu_si512(stride + 1 * BUSTER_EXECUTABLE_HASH_BLOCK_BYTES), key);
        a2 = executable_hash_round(a2, _mm512_loadu_si512(stride + 2 * BUSTER_EXECUTABLE_HASH_BLOCK_BYTES), key);
        a3 = executable_hash_round(a3, _mm512_loadu_si512(stride + 3 * BUSTER_EXECUTABLE_HASH_BLOCK_BYTES), key);
        a4 = executable_hash_round(a4, _mm512_loadu_si512(stride + 4 * BUSTER_EXECUTABLE_HASH_BLOCK_BYTES), key);
        a5 = executable_hash_round(a5, _mm512_loadu_si512(stride + 5 * BUSTER_EXECUTABLE_HASH_BLOCK_BYTES), key);
        a6 = executable_hash_round(a6, _mm512_loadu_si512(stride + 6 * BUSTER_EXECUTABLE_HASH_BLOCK_BYTES), key);
        a7 = executable_hash_round(a7, _mm512_loadu_si512(stride + 7 * BUSTER_EXECUTABLE_HASH_BLOCK_BYTES), key);
    }
    hash->accumulators[0] = a0;
    hash->accumulators[1] = a1;
    hash->accumulators[2] = a2;
    hash->accumulators[3] = a3;
    hash->accumulators[4] = a4;
    hash->accumulators[5] = a5;
    hash->accumulators[6] = a6;
    hash->accumulators[7] = a7;
}

// Block number `block` of the stream, whole, from `bytes`.
static void executable_hash_block(ExecutableHash* hash, const uint8_t* bytes, uint64_t block)
{
    const uint64_t i = block % BUSTER_EXECUTABLE_HASH_LANES;
    hash->accumulators[i] = executable_hash_round(hash->accumulators[i], _mm512_loadu_si512(bytes), executable_hash_key());
}

// The next `length` bytes of the stream. Every load is within the bytes, or within the carry.
static void executable_hash_bytes(ExecutableHash* hash, const uint8_t* bytes, uint64_t length)
{
    uint64_t used = 0;
    const uint64_t carried = hash->length % BUSTER_EXECUTABLE_HASH_BLOCK_BYTES;
    if (carried != 0)
    {
        const uint64_t room = BUSTER_EXECUTABLE_HASH_BLOCK_BYTES - carried;
        used = length < room ? length : room;
        memcpy(hash->carry + carried, bytes, used);
        if (used == room)
        {
            executable_hash_block(hash, hash->carry, hash->length / BUSTER_EXECUTABLE_HASH_BLOCK_BYTES);
        }
    }
    uint64_t block = (hash->length + used) / BUSTER_EXECUTABLE_HASH_BLOCK_BYTES;
    // Single blocks up to a stride's start, whole strides, then single blocks again.
    while (length - used >= BUSTER_EXECUTABLE_HASH_BLOCK_BYTES && block % BUSTER_EXECUTABLE_HASH_LANES != 0)
    {
        executable_hash_block(hash, bytes + used, block);
        used += BUSTER_EXECUTABLE_HASH_BLOCK_BYTES;
        block += 1;
    }
    const uint64_t strides = (length - used) / BUSTER_EXECUTABLE_HASH_STRIDE_BYTES;
    executable_hash_strides(hash, bytes + used, strides);
    used += strides * BUSTER_EXECUTABLE_HASH_STRIDE_BYTES;
    block += strides * BUSTER_EXECUTABLE_HASH_LANES;
    while (length - used >= BUSTER_EXECUTABLE_HASH_BLOCK_BYTES)
    {
        executable_hash_block(hash, bytes + used, block);
        used += BUSTER_EXECUTABLE_HASH_BLOCK_BYTES;
        block += 1;
    }
    memcpy(hash->carry + (hash->length + used) % BUSTER_EXECUTABLE_HASH_BLOCK_BYTES, bytes + used, length - used);
    hash->length += length;
}

static void executable_hash_end(ExecutableHash* hash, uint8_t* output)
{
    const uint64_t carried = hash->length % BUSTER_EXECUTABLE_HASH_BLOCK_BYTES;
    if (carried != 0)
    {
        memset(hash->carry + carried, 0, BUSTER_EXECUTABLE_HASH_BLOCK_BYTES - carried);
        executable_hash_block(hash, hash->carry, hash->length / BUSTER_EXECUTABLE_HASH_BLOCK_BYTES);
    }
    __m512i x = hash->accumulators[0];
    for (uint32_t i = 1; i < BUSTER_EXECUTABLE_HASH_LANES; i += 1)
    {
        x = _mm512_aesenc_epi128(x, hash->accumulators[i]);
    }
    const __m512i length_key = _mm512_xor_si512(_mm512_load_si512(executable_hash_constants + 40), _mm512_set1_epi64((long long)hash->length));
    x = _mm512_aesenc_epi128(_mm512_aesenc_epi128(x, length_key), executable_hash_key());
    const __m128i key = _mm512_castsi512_si128(executable_hash_key());
    __m128i folded = _mm512_castsi512_si128(x);
    folded = _mm_aesenc_si128(folded, _mm512_extracti32x4_epi32(x, 1));
    folded = _mm_aesenc_si128(folded, _mm512_extracti32x4_epi32(x, 2));
    folded = _mm_aesenc_si128(folded, _mm512_extracti32x4_epi32(x, 3));
    folded = _mm_aesenclast_si128(_mm_aesenc_si128(folded, key), key);
    _mm_storeu_si128((__m128i*)output, folded);
}

// ---------------------------------------------------------------------------------------------------------------------
// Patching
//
// What the patch reads: the fixup columns, the symbols' references, sizes and GOT slots, and where the GOT is. Most
// fixups are CALL and PC32: S + A - P into 4 bytes, S being the target's reference. Vector compares of an atom's kinds,
// 64 at a time, find its others (any other kind, or a wide addend). An atom without them goes through
// executable_patch_pc32, a loop built for CALL and PC32 alone (0.37 ns a fixup in L1 on the 9700X, against 0.48 with a
// branch on the kind per fixup and 0.60 with a range check of each value). An atom with them goes through
// executable_patch_mixed, which takes each fixup by its kind, CALL and PC32 inline and the others through
// executable_patch_other, once each (docs/object-writer-and-linker.md, "Patching and writing", has the variants it was
// chosen over). Neither checks a CALL's or PC32's S + A - P: executable_write does that once for every fixup, from the
// largest addend, the image's size and the farthest absolute symbols, and runs every fixup through
// executable_patch_other when it can't rule out a value outside int32.

typedef struct ExecutablePatch ExecutablePatch;
struct ExecutablePatch
{
    const uint32_t* offsets;
    const SymbolId* targets;
    const int32_t* addends;
    const uint8_t* kinds;
    const int64_t* wide_addends;
    const uint32_t* reference;
    const uint32_t* got_slots;
    const uint64_t* sizes;
    // The image's base address (BUSTER_EXECUTABLE_BASE, or 0 for a dynamic PIE) and the GOT's image offset.
    uint64_t base;
    uint64_t got;
    // With the static runtime, the link's __tls_get_addr, or 0: each call of it is a general- or local-dynamic access's,
    // which the access's relaxation consumes (executable_relax_tls).
    SymbolId relaxed;
    // In a static PIE, the bitset of the symbols that the GOT's loads and calls relax to (executable_relaxes), or 0.
    const uint64_t* relaxable;
    // Whether every fixup goes through executable_patch_other, which checks each value's range.
    bool checked;
};

// S + A - P into each 4-byte field of fixups [begin, end) of an atom at image offset `address` whose bytes are `bytes`,
// taking every fixup as CALL or PC32.
static void executable_patch_pc32(const ExecutablePatch* patch, uint8_t* bytes, uint32_t address, FixupId begin, FixupId end)
{
    const uint32_t* offsets = patch->offsets;
    const SymbolId* targets = patch->targets;
    const int32_t* addends = patch->addends;
    const uint32_t* reference = patch->reference;
    for (FixupId i = begin; i < end; i += 1)
    {
        const uint32_t value = reference[targets[i]] + (uint32_t)addends[i] - (address + offsets[i]);
        memcpy(bytes + offsets[i], &value, sizeof(value));
    }
}

// The value of a symbol's reference as an address: the base plus the image offset, which can be negative (the image
// offset of address 0, for a weak symbol defined nowhere).
static int64_t executable_address(const ExecutablePatch* patch, SymbolId symbol)
{
    return (int64_t)patch->base + (int32_t)patch->reference[symbol];
}

// A general- or local-dynamic TLS access (a TLSGD or TLSLD fixup, `fixup`, of the fixups [begin, end) of an atom whose
// bytes are `bytes`) relaxed to local-exec, as GNU ld and lld relax them in an executable: whether it is one of the
// sequences they take, its call of __tls_get_addr (patch->relaxed) the next fixup, through the PLT or the GOT (a
// CALL_GOT fixup, or a GOTPCREL one, which GNU as 2.42 writes for the general-dynamic access's prefixed call), with
// the bytes they take. The general-dynamic access, 66 48 8d 3d <x@tlsgd> then 66 66 48 e8 <__tls_get_addr@plt> or
// 66 48 ff 15 <__tls_get_addr@gotpcrel>, 16 bytes, becomes mov %fs:0, %rax; lea x@tpoff(%rax), %rax (64 48 8b 04 25
// 00 00 00 00 48 8d 80 <x@tpoff>); the local-dynamic one, 48 8d 3d <x@tlsld> then e8 <...@plt> (12 bytes) or ff 15
// <...@gotpcrel> (13), mov %fs:0, %rax after three or four 0x66 prefixes, so that its DTPOFF32 fields, which then add
// to the thread pointer, hold offsets from it. The call's fixup patches nothing (executable_relaxed_call).
static bool executable_relax_tls(const ExecutablePatch* patch, uint8_t* bytes, FixupId begin, FixupId end, FixupId fixup)
{
    static const uint8_t gd_lea[4] = { 0x66, 0x48, 0x8d, 0x3d };
    static const uint8_t gd_plt[4] = { 0x66, 0x66, 0x48, 0xe8 };
    static const uint8_t gd_got[4] = { 0x66, 0x48, 0xff, 0x15 };
    static const uint8_t ld_lea[3] = { 0x48, 0x8d, 0x3d };
    static const uint8_t ld_plt[1] = { 0xe8 };
    static const uint8_t ld_got[2] = { 0xff, 0x15 };
    static const uint8_t gd_le[12] = { 0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x80 };
    static const uint8_t ld_le[13] = { 0x66, 0x66, 0x66, 0x66, 0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00 };
    const bool gd = (patch->kinds[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_TLSGD;
    const uint32_t offset = patch->offsets[fixup];
    const uint32_t lea_bytes = gd ? 4 : 3;
    const FixupId next = fixup + 1;
    const uint8_t next_kind = next < end ? patch->kinds[next] & BUSTER_LINK_FIXUP_KIND : 0;
    const bool plt = next_kind == BUSTER_LINK_FIXUP_CALL;
    const bool got = next_kind == BUSTER_LINK_FIXUP_CALL_GOT || next_kind == BUSTER_LINK_FIXUP_GOTPCREL;
    // The call's field after the access's: 8 bytes on for a general-dynamic one in either form, 5 or 6 for a
    // local-dynamic one.
    const uint32_t distance = gd ? 8 : (plt ? 5 : 6);
    const uint32_t call_bytes = gd ? 4 : (plt ? 1 : 2);
    const uint8_t* call_form = gd ? (plt ? gd_plt : gd_got) : (plt ? ld_plt : ld_got);
    const bool call = (plt || got) && patch->relaxed != 0 && patch->targets[next] == patch->relaxed && patch->offsets[next] == offset + distance &&
                      offset >= lea_bytes && (patch->kinds[fixup] & BUSTER_LINK_FIXUP_WIDE) == 0 && patch->addends[fixup] == -4;
    const bool matches = call && memcmp(bytes + offset - lea_bytes, gd ? gd_lea : ld_lea, lea_bytes) == 0 &&
                         memcmp(bytes + offset + distance - call_bytes, call_form, call_bytes) == 0;
    if (matches && gd)
    {
        const int32_t tpoff = (int32_t)patch->reference[patch->targets[fixup]];
        memcpy(bytes + offset - lea_bytes, gd_le, sizeof(gd_le));
        memcpy(bytes + offset + distance, &tpoff, sizeof(tpoff));
    }
    else if (matches)
    {
        // Three prefixes before the PLT's form's 12 bytes, four before the GOT's 13.
        const uint32_t prefixes = plt ? 1 : 0;
        memcpy(bytes + offset - lea_bytes, ld_le + prefixes, sizeof(ld_le) - prefixes);
    }
    return matches && begin <= fixup;
}

// Whether a fixup, of kind `kind` with its BUSTER_LINK_FIXUP_WIDE bit and its addend column, relaxes to reach `target`
// directly: in a static PIE, a CALL_GOT or a LOAD_GOT whose addend is -4, to a symbol of the image's that isn't an
// IFUNC (patch->relaxable), as GNU ld and lld relax them in an executable. A static PIE needs it: its GOT's slots are
// relocated by the program's own startup (glibc's _dl_relocate_static_pie), which rcrt1.o's _start reaches by a call
// through __libc_start_main's slot, and libc.a's code before it reads others; both linkers' links without relaxation
// fault there.
static bool executable_relaxes(const ExecutablePatch* patch, uint8_t kind, int32_t addend, SymbolId target)
{
    const bool loads = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT;
    return patch->relaxable != 0 && loads && addend == -4 && (patch->relaxable[target / 64] >> (target % 64) & 1) != 0;
}

// Whether a relaxing CALL_GOT, its field at `offset` of `bytes`, is a jmp: ff 25, or e9 once relaxed (the forms
// executable_relax_got writes are taken for theirs too, so that a link written again relaxes alike).
static bool executable_got_jump(const uint8_t* bytes, uint32_t offset, uint8_t kind)
{
    const bool call_got = (kind & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_CALL_GOT;
    return call_got && (bytes[offset - 2] == 0xff ? bytes[offset - 1] == 0x25 : bytes[offset - 2] == 0xe9);
}

// Rewrites a relaxing CALL_GOT's or LOAD_GOT's instruction (executable_relaxes), its field at `offset` of `bytes`, to
// reach its target directly, with `field` the new form's rel32, as GNU ld and lld rewrite them: call [rip + slot]
// (ff 15) to addr32 call (67 e8), the same field; jmp [rip + slot] (ff 25) to jmp (e9) a byte earlier, its rel32 S + A
// - P + 1, and a nop after; mov r64, [rip + slot] (REX.W 8b) to lea (8d). The import and link_add_code_fixups give the
// kinds only these instructions, whose fields end them (executable_fixup_extent's bytes).
static void executable_relax_got(uint8_t* bytes, uint32_t offset, uint8_t kind, bool jump, uint32_t field)
{
    if ((kind & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_LOAD_GOT)
    {
        bytes[offset - 2] = 0x8d;
        memcpy(bytes + offset, &field, sizeof(field));
    }
    else if (jump)
    {
        bytes[offset - 2] = 0xe9;
        memcpy(bytes + offset - 1, &field, sizeof(field));
        bytes[offset + 3] = 0x90;
    }
    else
    {
        bytes[offset - 2] = 0x67;
        bytes[offset - 1] = 0xe8;
        memcpy(bytes + offset, &field, sizeof(field));
    }
}

// Whether a call of __tls_get_addr (patch->relaxed), fixup `fixup` of [begin, end), is a general- or local-dynamic
// access's, which executable_relax_tls relaxed with it: the fixup before it is the access's, which checked the call.
static bool executable_relaxed_call(const ExecutablePatch* patch, FixupId begin, FixupId fixup)
{
    const uint8_t previous = fixup > begin ? patch->kinds[fixup - 1] & BUSTER_LINK_FIXUP_KIND : 0;
    return previous == BUSTER_LINK_FIXUP_TLSGD || previous == BUSTER_LINK_FIXUP_TLSLD;
}

// Fixup `fixup` of an atom at image offset `address`, of any kind and either addend, among the atom's fixups [begin,
// end) that the patch takes (a TLS access's relaxation reads its call's): whether its value fits its field. The patch
// comes by value, so that once inlined its columns stay in registers across the stores to the bytes, which could alias
// anything.
BUSTER_INLINE bool executable_patch_other(ExecutablePatch patch, uint8_t* bytes, uint32_t address, FixupId begin, FixupId end, FixupId fixup)
{
    const uint8_t kind = patch.kinds[fixup] & BUSTER_LINK_FIXUP_KIND;
    const SymbolId target = patch.targets[fixup];
    const int32_t addend_column = patch.addends[fixup];
    const int64_t addend = (patch.kinds[fixup] & BUSTER_LINK_FIXUP_WIDE) != 0 ? patch.wide_addends[(uint32_t)addend_column] : addend_column;
    const uint32_t offset = patch.offsets[fixup];
    // P, S and G as addresses: P and G are in the image, below 2^31, and S is as executable_address gives it.
    const int64_t place = (int64_t)(patch.base + address + offset);
    const int64_t symbol = executable_address(&patch, target);
    const int64_t slot = (int64_t)(patch.base + patch.got) + 8 * (int64_t)patch.got_slots[target];
    const bool relaxes = executable_relaxes(&patch, patch.kinds[fixup], addend_column, target);
    const bool jump = relaxes && executable_got_jump(bytes, offset, kind);
    int64_t value = 0;
    // The value's width and range: 8 bytes, 4 that must sign-extend to it, or 4 that must zero-extend to it.
    uint32_t width = 4;
    bool zero_extended = false;
    switch (kind)
    {
        case BUSTER_LINK_FIXUP_CALL:
        case BUSTER_LINK_FIXUP_PC32:
            value = symbol + addend - place;
            break;
        case BUSTER_LINK_FIXUP_CALL_GOT:
        case BUSTER_LINK_FIXUP_LOAD_GOT:
            // A relaxing one's S + A - P, a jmp's from its end a byte nearer.
            value = relaxes ? symbol + addend - place + (jump ? 1 : 0) : slot + addend - place;
            break;
        case BUSTER_LINK_FIXUP_GOTPCREL:
        case BUSTER_LINK_FIXUP_GOTTPOFF:
            value = slot + addend - place;
            break;
        case BUSTER_LINK_FIXUP_TPOFF32:
            // A TLS symbol's reference is its offset from the thread pointer.
            value = (int32_t)patch.reference[target] + addend;
            break;
        case BUSTER_LINK_FIXUP_ABS64:
            value = symbol + addend;
            width = 8;
            break;
        case BUSTER_LINK_FIXUP_PC64:
            value = symbol + addend - place;
            width = 8;
            break;
        case BUSTER_LINK_FIXUP_ABS32:
            value = symbol + addend;
            zero_extended = true;
            break;
        case BUSTER_LINK_FIXUP_ABS32S:
            value = symbol + addend;
            break;
        case BUSTER_LINK_FIXUP_SIZE32:
            value = (int64_t)patch.sizes[target] + addend;
            zero_extended = true;
            break;
        case BUSTER_LINK_FIXUP_SIZE64:
            value = (int64_t)patch.sizes[target] + addend;
            width = 8;
            break;
        case BUSTER_LINK_FIXUP_DTPOFF32:
            // After a local-dynamic access's relaxation, its base is the thread pointer.
            value = (int32_t)patch.reference[target] + addend;
            break;
        default:
            // No other kind fits a field: link_check refuses kinds that don't exist, and TLSGD and TLSLD, relaxed with
            // their calls below, write their sequences' bytes.
            width = 0;
            break;
    }
    // A TLS access's relaxation, and its call, which writes nothing (the access did).
    const bool tls_access = kind == BUSTER_LINK_FIXUP_TLSGD || kind == BUSTER_LINK_FIXUP_TLSLD;
    const bool relaxed_call = !tls_access && patch.relaxed != 0 && target == patch.relaxed;
    width = tls_access || relaxed_call ? 0 : width;
    const bool sequence = tls_access ? executable_relax_tls(&patch, bytes, begin, end, fixup) : relaxed_call && executable_relaxed_call(&patch, begin, fixup);
    const bool fits = sequence || width == 8 || (width == 4 && (zero_extended ? (uint64_t)value <= UINT32_MAX : value == (int32_t)value));
    // A value that doesn't fit fails the link, and its field is zeroed over what executable_patch_pc32 wrote, so that
    // either path leaves the same bytes.
    value = fits ? value : 0;
    if (sequence)
    {
        // The relaxation wrote the access's bytes, its call's among them.
    }
    else if (relaxes)
    {
        executable_relax_got(bytes, offset, kind, jump, (uint32_t)value);
    }
    else if (width == 8)
    {
        memcpy(bytes + offset, &value, 8);
    }
    else
    {
        const uint32_t narrow = (uint32_t)value;
        memcpy(bytes + offset, &narrow, 4);
    }
    return fits;
}

// The fixups of [begin, end) that aren't CALL or PC32 with an addend in its column, in a mask per 64 from begin.
static uint64_t executable_other_kinds(const uint8_t* kinds, FixupId begin, FixupId end)
{
    const uint64_t count = end - begin;
    const __mmask64 valid = count >= 64 ? ~0ull : (1ull << count) - 1;
    const __m512i loaded = _mm512_maskz_loadu_epi8(valid, kinds + begin);
    const __mmask64 call = _mm512_cmpeq_epi8_mask(loaded, _mm512_set1_epi8(BUSTER_LINK_FIXUP_CALL));
    const __mmask64 pc32 = _mm512_cmpeq_epi8_mask(loaded, _mm512_set1_epi8(BUSTER_LINK_FIXUP_PC32));
    return valid & ~(call | pc32);
}

// Fixups [begin, end) of an atom at image offset `address`, each by its kind, unchecked: CALL and PC32 as
// executable_patch_pc32 does them, and the other kinds a compiled program has most, with an addend in its column, in
// the same bytes executable_patch_other writes: a static PIE's relaxing loads and calls (executable_relaxes) with their
// instructions' too. A GOT kind's G + A - P can't leave int32 either: G and P are in the image, as a PC32's S and P are. ABS64's 8 bytes take any value, and TPOFF32's offset from the thread pointer
// depends on the TLS block, which the image's size doesn't bound, so its range is checked: one that doesn't fit goes
// to executable_patch_other, as do the rest. Returns the first fixup whose value doesn't fit its field, or 0. The patch
// comes by value, as executable_patch_other's does.
BUSTER_INLINE FixupId executable_patch_mixed(ExecutablePatch patch, uint8_t* bytes, uint32_t address, FixupId begin, FixupId end)
{
    FixupId result = 0;
    const uint32_t got = (uint32_t)patch.got;
    for (FixupId i = begin; i < end; i += 1)
    {
        const uint8_t kind = patch.kinds[i];
        const uint32_t offset = patch.offsets[i];
        const SymbolId target = patch.targets[i];
        const int32_t addend = patch.addends[i];
        const int64_t tls = (int64_t)(int32_t)patch.reference[target] + addend;
        const bool relaxed = patch.relaxed != 0 && target == patch.relaxed;
        if ((kind == BUSTER_LINK_FIXUP_CALL || kind == BUSTER_LINK_FIXUP_PC32) && !relaxed)
        {
            const uint32_t value = patch.reference[target] + (uint32_t)addend - (address + offset);
            memcpy(bytes + offset, &value, sizeof(value));
        }
        else if (executable_relaxes(&patch, kind, addend, target))
        {
            const bool jump = executable_got_jump(bytes, offset, kind);
            const uint32_t value = patch.reference[target] + (uint32_t)addend - (address + offset) + (jump ? 1u : 0u);
            executable_relax_got(bytes, offset, kind, jump, value);
        }
        else if ((kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL || kind == BUSTER_LINK_FIXUP_GOTTPOFF) && !relaxed)
        {
            const uint32_t value = got + 8 * patch.got_slots[target] + (uint32_t)addend - (address + offset);
            memcpy(bytes + offset, &value, sizeof(value));
        }
        else if (kind == BUSTER_LINK_FIXUP_ABS64)
        {
            const int64_t value = (int64_t)patch.base + (int32_t)patch.reference[target] + addend;
            memcpy(bytes + offset, &value, sizeof(value));
        }
        else if (kind == BUSTER_LINK_FIXUP_TPOFF32 && tls == (int32_t)tls)
        {
            const uint32_t value = (uint32_t)tls;
            memcpy(bytes + offset, &value, sizeof(value));
        }
        else
        {
            const bool fits = executable_patch_other(patch, bytes, address, begin, end, i);
            result = result == 0 && !fits ? i : result;
        }
    }
    return result;
}

// Patches fixups [begin, end) of an atom at image offset `address`, all within `bytes`: through executable_patch_pc32
// if they are all CALL and PC32, which the vector compares of their kinds find 64 at a time, and otherwise through
// executable_patch_mixed. Returns the first fixup whose value doesn't fit its field, or 0.
static FixupId executable_patch_range(const ExecutablePatch* patch, uint8_t* bytes, uint32_t address, FixupId begin, FixupId end)
{
    const ExecutablePatch local = *patch;
    FixupId result = 0;
    if (local.checked)
    {
        for (FixupId i = begin; i < end; i += 1)
        {
            const bool fits = executable_patch_other(local, bytes, address, begin, end, i);
            result = result == 0 && !fits ? i : result;
        }
    }
    else
    {
        uint64_t others = 0;
        for (FixupId group = begin; group < end; group += 64)
        {
            others |= executable_other_kinds(local.kinds, group, end);
        }
        if (others == 0)
        {
            executable_patch_pc32(&local, bytes, address, begin, end);
        }
        else
        {
            result = executable_patch_mixed(local, bytes, address, begin, end);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The file

// A write of the output: pieces from file offset `offset`, or with `sequential` at the file's position, all of them,
// resumed after short writes and signals. Returns 0, or the errno of the call that failed.
static int executable_write_all(int file, const struct iovec* pieces, uint64_t piece_count, uint64_t offset, bool sequential)
{
    // IOV_MAX on Linux.
    enum { MAX_PIECES = 1024 };
    struct iovec window[MAX_PIECES];
    uint64_t next = 0;
    uint64_t skip = 0;
    int result = 0;
    while (result == 0 && next < piece_count)
    {
        uint64_t count = 0;
        for (uint64_t i = next; i < piece_count && count < MAX_PIECES; i += 1)
        {
            const uint64_t from = i == next ? skip : 0;
            window[count].iov_base = (uint8_t*)pieces[i].iov_base + from;
            window[count].iov_len = pieces[i].iov_len - from;
            count += 1;
        }
        const ssize_t written = sequential ? writev(file, window, (int)count) : pwritev(file, window, (int)count, (off_t)offset);
        if (written < 0 && errno != EINTR)
        {
            result = errno;
        }
        else if (written == 0)
        {
            result = EIO;
        }
        else if (written > 0)
        {
            offset += (uint64_t)written;
            uint64_t left = (uint64_t)written;
            while (next < piece_count && left >= pieces[next].iov_len - skip)
            {
                left -= pieces[next].iov_len - skip;
                skip = 0;
                next += 1;
            }
            skip += left;
        }
    }
    return result;
}

// The output, open for writing: its descriptor, how it is being written, and for a renamed one the name it is written
// under. Paths are copied with a NUL for the system calls.
#define BUSTER_EXECUTABLE_PATH_BYTES 4096
// The name of the file written beside the output, in its directory: its own, since the output's may leave no room under
// NAME_MAX for a suffix. The prefix, the process id, a dash and 16 random hexadecimal digits: at most 46 bytes and a NUL.
// And how many random names to try before giving up.
#define BUSTER_EXECUTABLE_TEMPORARY_PREFIX ".buster-executable-"
#define BUSTER_EXECUTABLE_TEMPORARY_NAME_BYTES 48
#define BUSTER_EXECUTABLE_NAME_ATTEMPTS 16

typedef struct ExecutableFile ExecutableFile;
struct ExecutableFile
{
    int descriptor;
    ExecutableWrite write;
    // The output's size and mode when it existed and is written in place.
    uint64_t old_size;
    uint32_t old_mode;
    char path[BUSTER_EXECUTABLE_PATH_BYTES];
    char written_path[BUSTER_EXECUTABLE_PATH_BYTES + BUSTER_EXECUTABLE_TEMPORARY_NAME_BYTES];
};

// Records a failed system call in the result.
static void executable_fail_file(ExecutableResult* result, const char* call, int error)
{
    if (result->error == EXECUTABLE_ERROR_NONE)
    {
        result->error = EXECUTABLE_ERROR_FILE;
        result->call = call;
        result->system_error = error;
    }
}

// Opens the output, by what lstat finds at its path: lstat opens nothing, which a FIFO would count as a reader or a
// writer, and needs no permission on the file.
//   - A regular file with one link that this process's user owns is opened with O_RDWR and written in place.
//   - One that is running (ETXTBSY), has other links or another owner, and a symbolic link (replaced, not written
//     through) get a new file in the output's directory, under a random name of its own, renamed over them at the end.
//   - A path with nothing there gets a new file, and a directory is an error.
//   - A file of another type (/dev/null, a FIFO) is opened once, with O_WRONLY, and written in order, as GNU ld and lld
//     write into one rather than replace it.
// fstat then checks what was opened, in case the path changed after lstat: a file opened in place that no longer
// qualifies, or a regular file opened as another type, is closed, and the output written beside it. Never O_TRUNC: on
// btrfs it costs 58 µs for 16 KB and 10 ms for 4 MB (docs/object-writer-and-linker.md, "The floors").
static bool executable_open(ExecutableFile* file, String8 path, ExecutableResult* result)
{
    bool opened = path.length != 0 && path.length < BUSTER_EXECUTABLE_PATH_BYTES;
    file->descriptor = -1;
    file->write = EXECUTABLE_WRITE_NONE;
    if (!opened)
    {
        executable_fail_file(result, "the path", ENAMETOOLONG);
    }
    else
    {
        memcpy(file->path, path.pointer, path.length);
        file->path[path.length] = 0;
        memcpy(file->written_path, file->path, path.length + 1);
        struct stat found;
        const int found_error = lstat(file->path, &found) == 0 ? 0 : errno;
        const mode_t type = found_error == 0 ? found.st_mode & S_IFMT : 0;
        const bool special = type != 0 && type != S_IFREG && type != S_IFLNK && type != S_IFDIR;
        if (type == S_IFREG && found.st_nlink == 1 && found.st_uid == geteuid())
        {
            file->descriptor = open(file->path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        }
        else if (special)
        {
            // A FIFO's open waits for a reader.
            file->descriptor = open(file->path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NOCTTY);
        }
        const int open_error = file->descriptor < 0 ? errno : 0;
        struct stat status;
        const bool known = file->descriptor >= 0 && fstat(file->descriptor, &status) == 0;
        const bool regular = known && S_ISREG(status.st_mode);
        if (!special && regular && status.st_nlink == 1 && status.st_uid == geteuid())
        {
            file->write = EXECUTABLE_WRITE_IN_PLACE;
            file->old_size = (uint64_t)status.st_size;
            file->old_mode = (uint32_t)status.st_mode & 07777;
        }
        else if (special && file->descriptor >= 0 && !regular)
        {
            file->write = EXECUTABLE_WRITE_SEQUENTIAL;
        }
        else if (special && file->descriptor < 0)
        {
            executable_fail_file(result, "open", open_error);
            opened = false;
        }
        else if (type == S_IFDIR)
        {
            executable_fail_file(result, "open", EISDIR);
            opened = false;
        }
        else if (found_error != 0 && found_error != ENOENT)
        {
            executable_fail_file(result, "lstat", found_error);
            opened = false;
        }
        else
        {
            if (file->descriptor >= 0)
            {
                close(file->descriptor);
            }
            // The path itself when nothing is there, once; otherwise random names in its directory, so that a file a killed
            // link left doesn't block the next ones.
            const bool absent = found_error == ENOENT;
            const char* slash = strrchr(file->path, '/');
            const uint64_t directory_bytes = slash != 0 ? (uint64_t)(slash - file->path) + 1 : 0;
            const uint32_t attempts = absent ? 1 : BUSTER_EXECUTABLE_NAME_ATTEMPTS;
            file->descriptor = -1;
            int create_error = EEXIST;
            for (uint32_t attempt = 0; file->descriptor < 0 && create_error == EEXIST && attempt < attempts; attempt += 1)
            {
                if (!absent)
                {
                    uint64_t random = 0;
                    if (getrandom(&random, sizeof(random), GRND_NONBLOCK) != (ssize_t)sizeof(random))
                    {
                        random = __rdtsc();
                    }
                    snprintf(file->written_path + directory_bytes, BUSTER_EXECUTABLE_TEMPORARY_NAME_BYTES, BUSTER_EXECUTABLE_TEMPORARY_PREFIX "%d-%016llx", (int)getpid(),
                             (unsigned long long)random);
                }
                file->descriptor = open(file->written_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0777);
                create_error = file->descriptor < 0 ? errno : 0;
            }
            file->write = absent ? EXECUTABLE_WRITE_CREATED : EXECUTABLE_WRITE_RENAMED;
            opened = file->descriptor >= 0;
            if (!opened)
            {
                executable_fail_file(result, "open", create_error);
                file->write = EXECUTABLE_WRITE_NONE;
            }
        }
    }
    return opened;
}

// Finishes the output: in place, its size and its mode (0777 less the umask, as a new file gets) if they changed; then
// closes it and, for a file written beside the output, renames it over the output. If the link failed (`keep` false),
// removes what was written instead, but for a file of another type, which was the user's. Returns whether it all
// succeeded.
static bool executable_close(ExecutableFile* file, uint64_t size, bool keep, ExecutableResult* result)
{
    bool kept = keep;
    if (kept && file->write == EXECUTABLE_WRITE_IN_PLACE)
    {
        // umask can only be read by setting it, and no other thread creates files meanwhile.
        const mode_t mask = umask(0);
        umask(mask);
        const uint32_t mode = 0777 & ~(uint32_t)mask;
        if (file->old_size != size && ftruncate(file->descriptor, (off_t)size) != 0)
        {
            executable_fail_file(result, "ftruncate", errno);
            kept = false;
        }
        if (kept && file->old_mode != mode && fchmod(file->descriptor, mode) != 0)
        {
            executable_fail_file(result, "fchmod", errno);
            kept = false;
        }
    }
    if (close(file->descriptor) != 0 && kept)
    {
        executable_fail_file(result, "close", errno);
        kept = false;
    }
    if (kept && file->write == EXECUTABLE_WRITE_RENAMED && rename(file->written_path, file->path) != 0)
    {
        executable_fail_file(result, "rename", errno);
        kept = false;
    }
    if (!kept && file->write != EXECUTABLE_WRITE_SEQUENTIAL)
    {
        unlink(file->written_path);
    }
    file->descriptor = -1;
    return kept;
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout
//
// The atoms are counted and placed by bucket: a class and an alignment, in the file's order. Classes go in their
// numbers' order (lld's), and within a class alignments go from the page's down to a byte, so that a bucket's atoms
// are placed by a prefix sum of their padded sizes. A function's alignment is at least 16, and the init and fini
// arrays' atoms all go in their class's bucket of 8, in id order.

// The sections' names, types and flags, for .shstrtab and the section headers.
static const char* const executable_section_names[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_INTERP] = ".interp",
    [EXECUTABLE_SECTION_ABI_NOTE] = ".note.ABI-tag",
    [EXECUTABLE_SECTION_BUILD_ID] = ".note.gnu.build-id",
    [EXECUTABLE_SECTION_DYNSYM] = ".dynsym",
    [EXECUTABLE_SECTION_VERSYM] = ".gnu.version",
    [EXECUTABLE_SECTION_VERNEED] = ".gnu.version_r",
    [EXECUTABLE_SECTION_GNU_HASH] = ".gnu.hash",
    [EXECUTABLE_SECTION_DYNSTR] = ".dynstr",
    [EXECUTABLE_SECTION_RELA_DYN] = ".rela.dyn",
    [EXECUTABLE_SECTION_RELR_DYN] = ".relr.dyn",
    [EXECUTABLE_SECTION_RELA_IPLT] = ".rela.iplt",
    [EXECUTABLE_SECTION_RODATA] = ".rodata",
    [EXECUTABLE_SECTION_EH_FRAME_HDR] = ".eh_frame_hdr",
    [EXECUTABLE_SECTION_EH_FRAME] = ".eh_frame",
    [EXECUTABLE_SECTION_TEXT] = ".text",
    [EXECUTABLE_SECTION_INIT] = ".init",
    [EXECUTABLE_SECTION_FINI] = ".fini",
    [EXECUTABLE_SECTION_PLT_GOT] = ".plt.got",
    [EXECUTABLE_SECTION_TDATA] = ".tdata",
    [EXECUTABLE_SECTION_TBSS] = ".tbss",
    [EXECUTABLE_SECTION_INIT_ARRAY] = ".init_array",
    [EXECUTABLE_SECTION_FINI_ARRAY] = ".fini_array",
    [EXECUTABLE_SECTION_DATA_RELRO] = ".data.rel.ro",
    [EXECUTABLE_SECTION_DYNAMIC] = ".dynamic",
    [EXECUTABLE_SECTION_GOT] = ".got",
    [EXECUTABLE_SECTION_DATA] = ".data",
    [EXECUTABLE_SECTION_BSS] = ".bss",
};

static const uint32_t executable_section_types[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_INTERP] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_ABI_NOTE] = SHT_NOTE,
    [EXECUTABLE_SECTION_BUILD_ID] = SHT_NOTE,
    [EXECUTABLE_SECTION_DYNSYM] = SHT_DYNSYM,
    [EXECUTABLE_SECTION_VERSYM] = SHT_GNU_versym,
    [EXECUTABLE_SECTION_VERNEED] = SHT_GNU_verneed,
    [EXECUTABLE_SECTION_GNU_HASH] = SHT_GNU_HASH,
    [EXECUTABLE_SECTION_DYNSTR] = SHT_STRTAB,
    [EXECUTABLE_SECTION_RELA_DYN] = SHT_RELA,
    [EXECUTABLE_SECTION_RELR_DYN] = SHT_RELR,
    [EXECUTABLE_SECTION_RELA_IPLT] = SHT_RELA,
    [EXECUTABLE_SECTION_RODATA] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_EH_FRAME_HDR] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_EH_FRAME] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_TEXT] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_INIT] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_FINI] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_PLT_GOT] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_TDATA] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_TBSS] = SHT_NOBITS,
    [EXECUTABLE_SECTION_INIT_ARRAY] = SHT_INIT_ARRAY,
    [EXECUTABLE_SECTION_FINI_ARRAY] = SHT_FINI_ARRAY,
    [EXECUTABLE_SECTION_DATA_RELRO] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_DYNAMIC] = SHT_DYNAMIC,
    [EXECUTABLE_SECTION_GOT] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_DATA] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_BSS] = SHT_NOBITS,
};

static const uint64_t executable_section_flags[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_INTERP] = SHF_ALLOC,
    [EXECUTABLE_SECTION_ABI_NOTE] = SHF_ALLOC,
    [EXECUTABLE_SECTION_BUILD_ID] = SHF_ALLOC,
    [EXECUTABLE_SECTION_DYNSYM] = SHF_ALLOC,
    [EXECUTABLE_SECTION_VERSYM] = SHF_ALLOC,
    [EXECUTABLE_SECTION_VERNEED] = SHF_ALLOC,
    [EXECUTABLE_SECTION_GNU_HASH] = SHF_ALLOC,
    [EXECUTABLE_SECTION_DYNSTR] = SHF_ALLOC,
    [EXECUTABLE_SECTION_RELA_DYN] = SHF_ALLOC,
    [EXECUTABLE_SECTION_RELR_DYN] = SHF_ALLOC,
    [EXECUTABLE_SECTION_RELA_IPLT] = SHF_ALLOC | SHF_INFO_LINK,
    [EXECUTABLE_SECTION_RODATA] = SHF_ALLOC,
    [EXECUTABLE_SECTION_EH_FRAME_HDR] = SHF_ALLOC,
    [EXECUTABLE_SECTION_EH_FRAME] = SHF_ALLOC,
    [EXECUTABLE_SECTION_TEXT] = SHF_ALLOC | SHF_EXECINSTR,
    [EXECUTABLE_SECTION_INIT] = SHF_ALLOC | SHF_EXECINSTR,
    [EXECUTABLE_SECTION_FINI] = SHF_ALLOC | SHF_EXECINSTR,
    [EXECUTABLE_SECTION_PLT_GOT] = SHF_ALLOC | SHF_EXECINSTR,
    [EXECUTABLE_SECTION_TDATA] = SHF_ALLOC | SHF_WRITE | SHF_TLS,
    [EXECUTABLE_SECTION_TBSS] = SHF_ALLOC | SHF_WRITE | SHF_TLS,
    [EXECUTABLE_SECTION_INIT_ARRAY] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_FINI_ARRAY] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_DATA_RELRO] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_DYNAMIC] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_GOT] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_DATA] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_BSS] = SHF_ALLOC | SHF_WRITE,
};

// The linker's own sections: their alignments, their entries' size (sh_entsize) and the section whose index sh_link
// gives, and the class before whose atoms each goes in the file (BUSTER_LINK_CLASS_COUNT for a section of atoms).
static const uint64_t executable_linker_alignment[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_INTERP] = 1,
    [EXECUTABLE_SECTION_ABI_NOTE] = 4,
    [EXECUTABLE_SECTION_BUILD_ID] = 4,
    [EXECUTABLE_SECTION_DYNSYM] = 8,
    [EXECUTABLE_SECTION_VERSYM] = 2,
    [EXECUTABLE_SECTION_VERNEED] = 4,
    [EXECUTABLE_SECTION_GNU_HASH] = 8,
    [EXECUTABLE_SECTION_DYNSTR] = 1,
    [EXECUTABLE_SECTION_RELA_DYN] = 8,
    [EXECUTABLE_SECTION_RELR_DYN] = 8,
    [EXECUTABLE_SECTION_RELA_IPLT] = 8,
    [EXECUTABLE_SECTION_EH_FRAME_HDR] = 4,
    [EXECUTABLE_SECTION_PLT_GOT] = 8,
    [EXECUTABLE_SECTION_DYNAMIC] = 8,
    [EXECUTABLE_SECTION_GOT] = 8,
};

static const uint64_t executable_section_entry_bytes[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_DYNSYM] = sizeof(Elf64_Sym),
    [EXECUTABLE_SECTION_VERSYM] = sizeof(Elf64_Half),
    [EXECUTABLE_SECTION_RELA_DYN] = sizeof(Elf64_Rela),
    [EXECUTABLE_SECTION_RELR_DYN] = sizeof(uint64_t),
    [EXECUTABLE_SECTION_RELA_IPLT] = sizeof(Elf64_Rela),
    [EXECUTABLE_SECTION_PLT_GOT] = BUSTER_EXECUTABLE_STUB_BYTES,
    [EXECUTABLE_SECTION_INIT_ARRAY] = 8,
    [EXECUTABLE_SECTION_FINI_ARRAY] = 8,
    [EXECUTABLE_SECTION_DYNAMIC] = sizeof(Elf64_Dyn),
    [EXECUTABLE_SECTION_GOT] = 8,
};

static const uint8_t executable_section_links[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_DYNSYM] = EXECUTABLE_SECTION_DYNSTR,
    [EXECUTABLE_SECTION_VERSYM] = EXECUTABLE_SECTION_DYNSYM,
    [EXECUTABLE_SECTION_VERNEED] = EXECUTABLE_SECTION_DYNSTR,
    [EXECUTABLE_SECTION_GNU_HASH] = EXECUTABLE_SECTION_DYNSYM,
    [EXECUTABLE_SECTION_RELA_DYN] = EXECUTABLE_SECTION_DYNSYM,
    [EXECUTABLE_SECTION_DYNAMIC] = EXECUTABLE_SECTION_DYNSTR,
};

static const uint8_t executable_linker_before[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_INTERP] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_ABI_NOTE] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_BUILD_ID] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_DYNSYM] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_VERSYM] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_VERNEED] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_GNU_HASH] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_DYNSTR] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_RELA_DYN] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_RELR_DYN] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_RELA_IPLT] = BUSTER_LINK_CLASS_RODATA,
    [EXECUTABLE_SECTION_RODATA] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_EH_FRAME_HDR] = BUSTER_LINK_CLASS_EH_FRAME,
    [EXECUTABLE_SECTION_EH_FRAME] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_TEXT] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_INIT] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_FINI] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_PLT_GOT] = BUSTER_LINK_CLASS_TDATA,
    [EXECUTABLE_SECTION_TDATA] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_TBSS] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_INIT_ARRAY] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_FINI_ARRAY] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_DATA_RELRO] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_DYNAMIC] = BUSTER_LINK_CLASS_DATA,
    [EXECUTABLE_SECTION_GOT] = BUSTER_LINK_CLASS_DATA,
    [EXECUTABLE_SECTION_DATA] = BUSTER_LINK_CLASS_COUNT,
    [EXECUTABLE_SECTION_BSS] = BUSTER_LINK_CLASS_COUNT,
};

#define BUSTER_EXECUTABLE_ALIGNMENTS (BUSTER_EXECUTABLE_PAGE_LOG2 + 1)
#define BUSTER_EXECUTABLE_BUCKETS ((BUSTER_LINK_CLASS_COUNT - 1) * BUSTER_EXECUTABLE_ALIGNMENTS)
#define BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2 3

// The section each class's atoms go to: in a static executable without the C runtime, EXECUTABLE_SECTION_COUNT for the
// runtime's classes (.init's and .fini's), which it doesn't have; in one with it (`crt`), or a dynamic PIE, every
// class's.
static const uint8_t executable_class_sections[BUSTER_LINK_CLASS_COUNT] = {
    [BUSTER_LINK_CLASS_NONE] = EXECUTABLE_SECTION_COUNT,
    [BUSTER_LINK_CLASS_RODATA] = EXECUTABLE_SECTION_RODATA,
    [BUSTER_LINK_CLASS_STRINGS] = EXECUTABLE_SECTION_RODATA,
    [BUSTER_LINK_CLASS_EH_FRAME] = EXECUTABLE_SECTION_EH_FRAME,
    [BUSTER_LINK_CLASS_TEXT] = EXECUTABLE_SECTION_TEXT,
    [BUSTER_LINK_CLASS_INIT] = EXECUTABLE_SECTION_COUNT,
    [BUSTER_LINK_CLASS_FINI] = EXECUTABLE_SECTION_COUNT,
    [BUSTER_LINK_CLASS_TDATA] = EXECUTABLE_SECTION_TDATA,
    [BUSTER_LINK_CLASS_TBSS] = EXECUTABLE_SECTION_TBSS,
    [BUSTER_LINK_CLASS_INIT_ARRAY] = EXECUTABLE_SECTION_INIT_ARRAY,
    [BUSTER_LINK_CLASS_FINI_ARRAY] = EXECUTABLE_SECTION_FINI_ARRAY,
    [BUSTER_LINK_CLASS_DATA_RELRO] = EXECUTABLE_SECTION_DATA_RELRO,
    [BUSTER_LINK_CLASS_DATA] = EXECUTABLE_SECTION_DATA,
    [BUSTER_LINK_CLASS_BSS] = EXECUTABLE_SECTION_BSS,
};

static const uint8_t executable_crt_class_sections[BUSTER_LINK_CLASS_COUNT] = {
    [BUSTER_LINK_CLASS_NONE] = EXECUTABLE_SECTION_COUNT,
    [BUSTER_LINK_CLASS_RODATA] = EXECUTABLE_SECTION_RODATA,
    [BUSTER_LINK_CLASS_STRINGS] = EXECUTABLE_SECTION_RODATA,
    [BUSTER_LINK_CLASS_EH_FRAME] = EXECUTABLE_SECTION_EH_FRAME,
    [BUSTER_LINK_CLASS_TEXT] = EXECUTABLE_SECTION_TEXT,
    [BUSTER_LINK_CLASS_INIT] = EXECUTABLE_SECTION_INIT,
    [BUSTER_LINK_CLASS_FINI] = EXECUTABLE_SECTION_FINI,
    [BUSTER_LINK_CLASS_TDATA] = EXECUTABLE_SECTION_TDATA,
    [BUSTER_LINK_CLASS_TBSS] = EXECUTABLE_SECTION_TBSS,
    [BUSTER_LINK_CLASS_INIT_ARRAY] = EXECUTABLE_SECTION_INIT_ARRAY,
    [BUSTER_LINK_CLASS_FINI_ARRAY] = EXECUTABLE_SECTION_FINI_ARRAY,
    [BUSTER_LINK_CLASS_DATA_RELRO] = EXECUTABLE_SECTION_DATA_RELRO,
    [BUSTER_LINK_CLASS_DATA] = EXECUTABLE_SECTION_DATA,
    [BUSTER_LINK_CLASS_BSS] = EXECUTABLE_SECTION_BSS,
};

static uint8_t executable_class_section(uint8_t class, bool crt)
{
    return crt ? executable_crt_class_sections[class] : executable_class_sections[class];
}

// Whether a class's atoms keep their id order without padding between them, as fragments of one piece: .init's and
// .fini's (with the C runtime), which run into each other, and .eh_frame's, whose records follow one another. Each must
// start at a multiple of its own alignment where the ones before it end, but .eh_frame's at a multiple of 4: its
// records' lengths are, and lld writes the records one after another whatever their sections' alignment.
static bool executable_class_is_ordered(uint8_t class, bool crt)
{
    return class == BUSTER_LINK_CLASS_EH_FRAME || (crt && (class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI));
}

// An atom's alignment as its bucket has it, from its class, alignment and size, or BUSTER_EXECUTABLE_ALIGNMENTS if it
// isn't one the layout takes. An ordered class's atoms all go in its bucket of a byte.
static uint32_t executable_alignment_log2(uint8_t class, uint8_t alignment_log2, uint64_t size, bool crt)
{
    uint32_t result = alignment_log2;
    if (class == BUSTER_LINK_CLASS_TEXT)
    {
        result = result < BUSTER_EXECUTABLE_FUNCTION_ALIGNMENT_LOG2 ? BUSTER_EXECUTABLE_FUNCTION_ALIGNMENT_LOG2 : result;
    }
    else if (class == BUSTER_LINK_CLASS_INIT_ARRAY || class == BUSTER_LINK_CLASS_FINI_ARRAY)
    {
        const bool entries = result <= BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2 && size % 8 == 0;
        result = entries ? BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2 : BUSTER_EXECUTABLE_ALIGNMENTS;
    }
    else if (executable_class_is_ordered(class, crt))
    {
        result = result <= BUSTER_EXECUTABLE_PAGE_LOG2 ? 0 : BUSTER_EXECUTABLE_ALIGNMENTS;
    }
    else if (executable_class_section(class, crt) == EXECUTABLE_SECTION_COUNT)
    {
        result = BUSTER_EXECUTABLE_ALIGNMENTS;
    }
    return result > BUSTER_EXECUTABLE_PAGE_LOG2 ? BUSTER_EXECUTABLE_ALIGNMENTS : result;
}

static uint32_t executable_bucket(uint8_t class, uint32_t alignment_log2)
{
    return (uint32_t)(class - 1) * BUSTER_EXECUTABLE_ALIGNMENTS + (BUSTER_EXECUTABLE_PAGE_LOG2 - alignment_log2);
}

// Whether an atom's bytes go in the file, from its class and size: it has some, and isn't zero-fill.
static bool executable_in_file(uint8_t class, uint64_t size)
{
    return size != 0 && !link_class_is_zero_fill(class);
}

// Which prioritized atoms an array class's are among (ExecutableCounts.prioritized_bytes): 0 for .init_array's, 1 for
// .fini_array's.
static uint32_t executable_array_index(uint8_t class)
{
    return class == BUSTER_LINK_CLASS_FINI_ARRAY ? 1 : 0;
}

// The first of link->priorities' entries (from 1, in atom order) whose atom is `atom` or one after it, or the count: a
// binary search.
static uint32_t executable_first_priority(const LinkPriorities* priorities, AtomId atom)
{
    uint32_t low = 1;
    uint32_t high = priorities->count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = priorities->atom[middle] < atom;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low > priorities->count ? priorities->count : low;
}

// The classes whose atoms the layout's vector passes take (executable_atom_block), the same in either kind of
// executable: those whose alignment is their atoms' own, or for TEXT a function's at least. EH_FRAME's and the C
// runtime's INIT and FINI keep id order, INIT_ARRAY's and FINI_ARRAY's atoms are entries, and a static executable has
// no section for INIT and FINI: their atoms go through the scalar loops.
#define BUSTER_EXECUTABLE_BLOCK_CLASSES                                                                                                                         \
    ((1u << BUSTER_LINK_CLASS_RODATA) | (1u << BUSTER_LINK_CLASS_STRINGS) | (1u << BUSTER_LINK_CLASS_TEXT) | (1u << BUSTER_LINK_CLASS_TDATA) |                  \
     (1u << BUSTER_LINK_CLASS_TBSS) | (1u << BUSTER_LINK_CLASS_DATA_RELRO) | (1u << BUSTER_LINK_CLASS_DATA) | (1u << BUSTER_LINK_CLASS_BSS))

// Up to 16 atoms from `first` (the `lanes` of a block), as executable_count and executable_place take them: taken if
// every one is of a class of BUSTER_EXECUTABLE_BLOCK_CLASSES, aligned to at most a page (as executable_alignment_log2
// raises a function's), no larger than BUSTER_EXECUTABLE_MAX_ATOM_BYTES, and in the same bucket, as a run of
// consecutive atoms mostly is. Then `bucket` is theirs, `sizes` their sizes and `padded` those padded to their
// alignment (lanes 0 to 7 and 8 to 15, a masked-off lane's 0), and `in_file` the lanes whose bytes are in the file
// (executable_in_file). A block that isn't taken goes through the scalar loop, atom by atom, which also finds any atom
// the layout refuses.
typedef struct ExecutableAtomBlock ExecutableAtomBlock;
struct ExecutableAtomBlock
{
    __m512i sizes[2];
    __m512i padded[2];
    uint32_t bucket;
    __mmask16 in_file;
    bool taken;
};

static ExecutableAtomBlock executable_atom_block(const LinkAtoms* atoms, AtomId first, __mmask16 lanes)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i classes = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, atoms->class + first));
    const __m512i alignments = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, atoms->alignment_log2 + first));
    const __m512i sizes[2] = {
        _mm512_maskz_loadu_epi64((__mmask8)lanes, atoms->size + first),
        _mm512_maskz_loadu_epi64((__mmask8)(lanes >> 8), atoms->size + first + 8),
    };
    // A class's bit of BUSTER_EXECUTABLE_BLOCK_CLASSES, shifted down to bit 0.
    const __m512i class_bits = _mm512_srlv_epi32(_mm512_set1_epi32((int)BUSTER_EXECUTABLE_BLOCK_CLASSES), classes);
    const __mmask16 eligible = _mm512_mask_test_epi32_mask(lanes, class_bits, _mm512_set1_epi32(1));
    const __mmask16 text = _mm512_mask_cmpeq_epi32_mask(lanes, classes, _mm512_set1_epi32(BUSTER_LINK_CLASS_TEXT));
    const __m512i log2 = _mm512_mask_max_epu32(alignments, text, alignments, _mm512_set1_epi32(BUSTER_EXECUTABLE_FUNCTION_ALIGNMENT_LOG2));
    const __mmask16 aligned = _mm512_mask_cmple_epu32_mask(lanes, log2, _mm512_set1_epi32(BUSTER_EXECUTABLE_PAGE_LOG2));
    const __m512i largest = _mm512_set1_epi64((long long)BUSTER_EXECUTABLE_MAX_ATOM_BYTES);
    const __mmask16 small = (__mmask16)(_mm512_mask_cmple_epu64_mask((__mmask8)lanes, sizes[0], largest) |
                                        (uint32_t)_mm512_mask_cmple_epu64_mask((__mmask8)(lanes >> 8), sizes[1], largest) << 8);
    // executable_bucket's, and whether every lane's is lane 0's.
    const __m512i buckets = _mm512_add_epi32(_mm512_mullo_epi32(_mm512_sub_epi32(classes, _mm512_set1_epi32(1)), _mm512_set1_epi32(BUSTER_EXECUTABLE_ALIGNMENTS)),
                                             _mm512_sub_epi32(_mm512_set1_epi32(BUSTER_EXECUTABLE_PAGE_LOG2), log2));
    const uint32_t bucket = (uint32_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(buckets));
    const __mmask16 same = _mm512_mask_cmpeq_epi32_mask(lanes, buckets, _mm512_set1_epi32((int)bucket));
    const __mmask16 nonzero = (__mmask16)(_mm512_mask_cmpneq_epu64_mask((__mmask8)lanes, sizes[0], zero) |
                                          (uint32_t)_mm512_mask_cmpneq_epu64_mask((__mmask8)(lanes >> 8), sizes[1], zero) << 8);
    const __mmask16 zero_fill = _mm512_mask_cmpeq_epi32_mask(lanes, classes, _mm512_set1_epi32(BUSTER_LINK_CLASS_TBSS)) |
                                _mm512_mask_cmpeq_epi32_mask(lanes, classes, _mm512_set1_epi32(BUSTER_LINK_CLASS_BSS));
    ExecutableAtomBlock result = {
        .sizes = { sizes[0], sizes[1] },
        .bucket = bucket,
        .in_file = nonzero & (__mmask16)~zero_fill,
        .taken = (eligible & aligned & small & same) == lanes,
    };
    // executable_align's: the size plus the alignment less one, without the alignment's low bits.
    for (uint32_t half = 0; half < 2; half += 1)
    {
        const __m512i half_log2 = _mm512_cvtepu32_epi64(half == 0 ? _mm512_castsi512_si256(log2) : _mm512_extracti64x4_epi64(log2, 1));
        const __m512i low_bits = _mm512_sub_epi64(_mm512_sllv_epi64(_mm512_set1_epi64(1), half_log2), _mm512_set1_epi64(1));
        result.padded[half] = _mm512_andnot_si512(low_bits, _mm512_add_epi64(sizes[half], low_bits));
    }
    return result;
}

// The lanes of a block of up to 16 records from `first`, whose end is `end`.
static __mmask16 executable_block_lanes(uint64_t first, uint64_t end)
{
    const uint64_t left = end - first;
    return left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
}

// The inclusive prefix sums of 8 64-bit lanes: three shifted adds.
static __m512i executable_scan64(__m512i x)
{
    const __m512i zero = _mm512_setzero_si512();
    __m512i result = _mm512_add_epi64(x, _mm512_alignr_epi64(x, zero, 7));
    result = _mm512_add_epi64(result, _mm512_alignr_epi64(result, zero, 6));
    result = _mm512_add_epi64(result, _mm512_alignr_epi64(result, zero, 4));
    return result;
}

// The last of 8 64-bit lanes.
static uint64_t executable_last64(__m512i x)
{
    return (uint64_t)_mm_cvtsi128_si64(_mm512_castsi512_si128(_mm512_permutexvar_epi64(_mm512_set1_epi64(7), x)));
}

// A piece of the file: an atom's bytes, or bytes of the linker's own (atom 0).
typedef struct ExecutablePiece ExecutablePiece;
struct ExecutablePiece
{
    uint64_t offset;
    uint64_t size;
    uint8_t* bytes;
    AtomId atom;
};

_Static_assert(sizeof(ExecutablePiece) == 32, "four 64-bit lanes a piece, for executable_store_pieces");

// The pieces of 8 atoms of a block (lanes 0 to 7 of their file offsets, sizes, bytes and ids, the ids zero-extended
// over the record's padding), stored from `placed` as records, two to a vector, interleaved by permutes: the first
// `count` of them.
static void executable_store_pieces(ExecutablePiece* placed, __m512i offsets, __m512i sizes, __m512i bytes, __m512i atoms, uint32_t count)
{
    const __m512i pairs_low = _mm512_setr_epi64(0, 8, 1, 9, 2, 10, 3, 11);
    const __m512i pairs_high = _mm512_setr_epi64(4, 12, 5, 13, 6, 14, 7, 15);
    const __m512i records_low = _mm512_setr_epi64(0, 1, 8, 9, 2, 3, 10, 11);
    const __m512i records_high = _mm512_setr_epi64(4, 5, 12, 13, 6, 7, 14, 15);
    const __m512i places[2] = {
        _mm512_permutex2var_epi64(offsets, pairs_low, sizes),
        _mm512_permutex2var_epi64(offsets, pairs_high, sizes),
    };
    const __m512i owners[2] = {
        _mm512_permutex2var_epi64(bytes, pairs_low, atoms),
        _mm512_permutex2var_epi64(bytes, pairs_high, atoms),
    };
    for (uint32_t pair = 0; 2 * pair < count; pair += 1)
    {
        const __m512i record = _mm512_permutex2var_epi64(places[pair / 2], pair % 2 == 0 ? records_low : records_high, owners[pair / 2]);
        const uint32_t records = count - 2 * pair >= 2 ? 2 : 1;
        _mm512_mask_storeu_epi64(placed + 2 * pair, (__mmask8)((1u << (4 * records)) - 1), record);
    }
}

// A section of the image: its image offset, file offset and size (in memory; in the file too, but for .tbss and
// .bss), and its alignment.
typedef struct ExecutableSectionLayout ExecutableSectionLayout;
struct ExecutableSectionLayout
{
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint64_t alignment;
};

// The segments, in program header order after PT_PHDR: R, RX, RELRO and RW.
typedef enum ExecutableSegment
{
    EXECUTABLE_SEGMENT_R,
    EXECUTABLE_SEGMENT_RX,
    EXECUTABLE_SEGMENT_RELRO,
    EXECUTABLE_SEGMENT_RW,
    EXECUTABLE_SEGMENTS,
} ExecutableSegment;

typedef struct ExecutableSegmentLayout ExecutableSegmentLayout;
struct ExecutableSegmentLayout
{
    uint64_t address;
    uint64_t offset;
    uint64_t file_size;
    uint64_t memory_size;
};

// Program headers: PT_PHDR, a dynamic PIE's PT_INTERP, a PT_LOAD per segment with bytes, PT_TLS with TLS, a PIE's
// PT_DYNAMIC, PT_GNU_RELRO with a RELRO segment, PT_GNU_EH_FRAME with .eh_frame_hdr, PT_GNU_STACK and PT_NOTE.
#define BUSTER_EXECUTABLE_MAX_PROGRAM_HEADERS 12
// Section headers: the null one, the sections with bytes or memory, and .shstrtab.
#define BUSTER_EXECUTABLE_MAX_SECTION_HEADERS (EXECUTABLE_SECTION_COUNT + 2)
#define BUSTER_EXECUTABLE_SHSTRTAB_BYTES 272

// What every lane computes alike from the counts: where the sections, the segments and each bucket go.
typedef struct ExecutableLayout ExecutableLayout;
struct ExecutableLayout
{
    // Whether it is a PIE, dynamic or static (`dynamic`, with .dynamic); whether it links the C runtime (`crt`: a PIE, or
    // a static executable with it); whether it links the static runtime (`runtime`: a static executable with the C
    // runtime, or a static PIE, whose stubs are its IFUNCs'); and the image's base address.
    bool dynamic;
    bool crt;
    bool runtime;
    uint64_t base;
    ExecutableSectionLayout sections[EXECUTABLE_SECTION_COUNT];
    // Each section's index among the section headers, 0 for one without bytes or memory.
    uint32_t section_index[EXECUTABLE_SECTION_COUNT];
    ExecutableSegmentLayout segments[EXECUTABLE_SEGMENTS];
    bool has_segment[EXECUTABLE_SEGMENTS];
    uint32_t program_headers;
    uint64_t header_bytes;
    // The TLS segment: its alignment, its memory size, and the thread pointer's offset from its start (the end of the
    // TLS block).
    uint64_t tls_alignment;
    uint64_t tls_size;
    uint64_t tls_block;
    // Each bucket's first image offset and first piece, summed over every lane: the bucket's start.
    uint64_t bucket_address[BUSTER_EXECUTABLE_BUCKETS];
    uint64_t bucket_piece[BUSTER_EXECUTABLE_BUCKETS];
    // Where the prioritized init and fini array atoms start, by executable_array_index: their class's start in its
    // section, before its buckets; and their first pieces.
    uint64_t prioritized_address[2];
    uint64_t prioritized_piece[2];
    // The GOT's slots, and in a static executable with the C runtime the first of its stubs' (after the GOT kinds'),
    // one per stub in stub order, which IRELATIVE relocations fill.
    uint64_t got_slots;
    uint64_t stub_slot;
    // The pieces of the linker's own: the headers (piece 0), each of its sections with bytes (0 for one without), and
    // .shstrtab and the section headers (the last two).
    uint64_t section_piece[EXECUTABLE_SECTION_COUNT];
    uint64_t shstrtab_offset;
    uint64_t shstrtab_size;
    uint64_t section_headers_offset;
    uint32_t section_header_count;
    uint64_t piece_count;
    uint64_t image_size;
    uint64_t file_size;
    uint64_t chunk_count;
};

// A section's name in this executable: the stubs of the static runtime (a static executable's with the C runtime, or a
// static PIE's) are its IFUNCs', .iplt, as lld names them.
static const char* executable_section_name(const ExecutableLayout* layout, uint32_t section)
{
    return layout->runtime && section == EXECUTABLE_SECTION_PLT_GOT ? ".iplt" : executable_section_names[section];
}

// The counts of one lane's atoms: per bucket, how many, their padded bytes and how many go in the file; per ordered
// class (executable_class_is_ordered), its atoms' largest alignment; the FDEs in its .eh_frame atoms; and the bytes
// and pieces of its prioritized init and fini array atoms (link->priorities), by executable_array_index, which no
// bucket counts (executable_place_prioritized places them).
typedef struct ExecutableCounts ExecutableCounts;
struct ExecutableCounts
{
    uint64_t bytes[BUSTER_EXECUTABLE_BUCKETS];
    uint64_t pieces[BUSTER_EXECUTABLE_BUCKETS];
    uint8_t ordered_log2[BUSTER_LINK_CLASS_COUNT];
    // The FDEs of the lane's .eh_frame atoms.
    uint64_t fdes;
    uint64_t prioritized_bytes[2];
    uint64_t prioritized_pieces[2];
};

// Places the sections, from every lane's counts, the GOT's slots and the sizes of the linker's other sections
// (`linker_bytes`, by section, or 0 for none: a static executable's without the C runtime): the buckets one after
// another within their sections, each section at its alignment, and the segments 4 KB-congruent from the next page.
// Returns false if the image reaches BUSTER_EXECUTABLE_MAX_ADDRESS or the TLS block is larger than
// BUSTER_EXECUTABLE_MAX_TLS_BYTES. The atoms are at most BUSTER_EXECUTABLE_MAX_ATOM_BYTES each, so no sum of their
// sizes wraps.
static bool executable_lay_out(ExecutableLayout* layout, const ExecutableCounts* counts, uint32_t lanes, uint64_t got_slots, bool dynamic, bool crt, bool runtime,
                               const uint64_t* linker_bytes)
{
    *layout = (ExecutableLayout){ 0 };
    layout->dynamic = dynamic;
    layout->crt = crt;
    layout->runtime = runtime;
    layout->base = dynamic ? 0 : BUSTER_EXECUTABLE_BASE;
    uint64_t bytes[BUSTER_EXECUTABLE_BUCKETS] = { 0 };
    uint64_t pieces[BUSTER_EXECUTABLE_BUCKETS] = { 0 };
    uint8_t ordered_log2[BUSTER_LINK_CLASS_COUNT] = { 0 };
    uint64_t prioritized_bytes[2] = { 0 };
    uint64_t prioritized_pieces[2] = { 0 };
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        for (uint32_t bucket = 0; bucket < BUSTER_EXECUTABLE_BUCKETS; bucket += 1)
        {
            bytes[bucket] += counts[lane].bytes[bucket];
            pieces[bucket] += counts[lane].pieces[bucket];
        }
        for (uint32_t array = 0; array < 2; array += 1)
        {
            prioritized_bytes[array] += counts[lane].prioritized_bytes[array];
            prioritized_pieces[array] += counts[lane].prioritized_pieces[array];
        }
        for (uint8_t class = 0; class < BUSTER_LINK_CLASS_COUNT; class += 1)
        {
            ordered_log2[class] = counts[lane].ordered_log2[class] > ordered_log2[class] ? counts[lane].ordered_log2[class] : ordered_log2[class];
        }
    }
    // Each section's size and alignment: the linker's own from what they hold, and the others from their buckets'.
    uint64_t section_bytes[EXECUTABLE_SECTION_COUNT] = { 0 };
    uint64_t section_alignment[EXECUTABLE_SECTION_COUNT];
    for (uint32_t section = 0; section < EXECUTABLE_SECTION_COUNT; section += 1)
    {
        section_bytes[section] = linker_bytes != 0 ? linker_bytes[section] : 0;
        section_alignment[section] = section_bytes[section] != 0 && executable_linker_alignment[section] != 0 ? executable_linker_alignment[section] : 1;
    }
    section_bytes[EXECUTABLE_SECTION_BUILD_ID] = BUSTER_EXECUTABLE_NOTE_BYTES;
    section_alignment[EXECUTABLE_SECTION_BUILD_ID] = 4;
    section_bytes[EXECUTABLE_SECTION_GOT] = got_slots != 0 ? 8 * (got_slots + 1) : 0;
    section_alignment[EXECUTABLE_SECTION_GOT] = 8;
    // STRINGS follow RODATA in .rodata, at their own largest alignment. An ordered class's alignment is its atoms'
    // largest, though they all go in its bucket of a byte. The init and fini arrays' prioritized atoms, entries, come
    // first in their sections.
    uint64_t strings_start = 0;
    for (uint8_t class = 1; class < BUSTER_LINK_CLASS_COUNT; class += 1)
    {
        const uint8_t section = executable_class_section(class, crt);
        const bool array = class == BUSTER_LINK_CLASS_INIT_ARRAY || class == BUSTER_LINK_CLASS_FINI_ARRAY;
        uint64_t class_alignment = 0;
        uint64_t class_bytes = array ? prioritized_bytes[executable_array_index(class)] : 0;
        for (uint32_t log2 = BUSTER_EXECUTABLE_PAGE_LOG2 + 1; log2-- > 0;)
        {
            const uint32_t bucket = executable_bucket(class, log2);
            class_alignment = class_alignment == 0 && bytes[bucket] != 0 ? 1ull << log2 : class_alignment;
            class_bytes += bytes[bucket];
        }
        class_alignment = class_alignment == 0 && class_bytes != 0 ? 1ull << BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2 : class_alignment;
        class_alignment = class_alignment != 0 && executable_class_is_ordered(class, crt) ? 1ull << ordered_log2[class] : class_alignment;
        if (class_alignment != 0 && section != EXECUTABLE_SECTION_COUNT)
        {
            const uint64_t start = executable_align(section_bytes[section], class_alignment);
            strings_start = class == BUSTER_LINK_CLASS_STRINGS ? start : strings_start;
            section_bytes[section] = start + class_bytes;
            section_alignment[section] = class_alignment > section_alignment[section] ? class_alignment : section_alignment[section];
        }
    }
    section_alignment[EXECUTABLE_SECTION_TEXT] = section_alignment[EXECUTABLE_SECTION_TEXT] < 16 ? 16 : section_alignment[EXECUTABLE_SECTION_TEXT];
    // The TLS segment starts at its whole alignment, so that offsets in it are offsets in the TLS block, whose start the
    // program aligns to it: its p_vaddr is a multiple of p_align, and the block's distance below the thread pointer,
    // align_up(p_memsz + f, p_align) - f for p_vaddr's remainder f (docs/object-writer-and-linker.md, "Layout"), is
    // p_memsz rounded up.
    layout->tls_alignment = section_alignment[EXECUTABLE_SECTION_TDATA] > section_alignment[EXECUTABLE_SECTION_TBSS] ? section_alignment[EXECUTABLE_SECTION_TDATA]
                                                                                                                      : section_alignment[EXECUTABLE_SECTION_TBSS];
    section_alignment[EXECUTABLE_SECTION_TDATA] = layout->tls_alignment;

    // Which segments have anything, and so the program headers.
    const bool has_tls = section_bytes[EXECUTABLE_SECTION_TDATA] + section_bytes[EXECUTABLE_SECTION_TBSS] != 0;
    layout->has_segment[EXECUTABLE_SEGMENT_R] = true;
    layout->has_segment[EXECUTABLE_SEGMENT_RX] = section_bytes[EXECUTABLE_SECTION_TEXT] + section_bytes[EXECUTABLE_SECTION_INIT] + section_bytes[EXECUTABLE_SECTION_FINI] +
                                                     section_bytes[EXECUTABLE_SECTION_PLT_GOT] !=
                                                 0;
    layout->has_segment[EXECUTABLE_SEGMENT_RELRO] = section_bytes[EXECUTABLE_SECTION_TDATA] + section_bytes[EXECUTABLE_SECTION_INIT_ARRAY] +
                                                        section_bytes[EXECUTABLE_SECTION_FINI_ARRAY] + section_bytes[EXECUTABLE_SECTION_DATA_RELRO] +
                                                        section_bytes[EXECUTABLE_SECTION_DYNAMIC] + section_bytes[EXECUTABLE_SECTION_GOT] !=
                                                    0;
    layout->has_segment[EXECUTABLE_SEGMENT_RW] = section_bytes[EXECUTABLE_SECTION_DATA] + section_bytes[EXECUTABLE_SECTION_BSS] != 0;
    layout->program_headers = 4 + (has_tls ? 1 : 0) + (layout->has_segment[EXECUTABLE_SEGMENT_RELRO] ? 1 : 0) + (dynamic ? 1 : 0) +
                              (section_bytes[EXECUTABLE_SECTION_INTERP] != 0 ? 1 : 0) + (section_bytes[EXECUTABLE_SECTION_EH_FRAME_HDR] != 0 ? 1 : 0);
    for (uint32_t segment = EXECUTABLE_SEGMENT_RX; segment < EXECUTABLE_SEGMENTS; segment += 1)
    {
        layout->program_headers += layout->has_segment[segment] ? 1 : 0;
    }
    layout->header_bytes = sizeof(Elf64_Ehdr) + layout->program_headers * sizeof(Elf64_Phdr);

    // The sections, each segment starting on the page after the last one's end at its offset's place in a page.
    static const uint8_t first_sections[EXECUTABLE_SEGMENTS] = {
        EXECUTABLE_SECTION_INTERP,
        EXECUTABLE_SECTION_TEXT,
        EXECUTABLE_SECTION_TDATA,
        EXECUTABLE_SECTION_DATA,
    };
    uint64_t offset = layout->header_bytes;
    uint64_t address = layout->header_bytes;
    uint32_t segment = EXECUTABLE_SEGMENT_R;
    layout->segments[EXECUTABLE_SEGMENT_R] = (ExecutableSegmentLayout){ 0 };
    for (uint32_t section = 0; section < EXECUTABLE_SECTION_COUNT; section += 1)
    {
        if (segment + 1 < EXECUTABLE_SEGMENTS && section == first_sections[segment + 1])
        {
            ExecutableSegmentLayout* previous = &layout->segments[segment];
            previous->file_size = offset - previous->offset;
            previous->memory_size = address - previous->address;
            segment += 1;
            address = executable_align(address, BUSTER_EXECUTABLE_PAGE_BYTES) + (offset & (BUSTER_EXECUTABLE_PAGE_BYTES - 1));
            layout->segments[segment].address = executable_align(address, section_alignment[section]);
            layout->segments[segment].offset = executable_align(offset, section_alignment[section]);
        }
        // A section without bits has the file offset where its bytes would start, as lld gives it, and takes none.
        const bool no_bits = section == EXECUTABLE_SECTION_TBSS || section == EXECUTABLE_SECTION_BSS;
        ExecutableSectionLayout* placed = &layout->sections[section];
        placed->alignment = section_alignment[section];
        placed->size = section_bytes[section];
        placed->offset = no_bits ? offset : executable_align(offset, section_alignment[section]);
        placed->address = executable_align(address, section_alignment[section]);
        if (section == EXECUTABLE_SECTION_TBSS)
        {
            // After .tdata in the TLS segment, but taking no address space: .init_array starts where .tbss does.
            placed->address = executable_align(layout->sections[EXECUTABLE_SECTION_TDATA].address + layout->sections[EXECUTABLE_SECTION_TDATA].size,
                                               section_alignment[section]);
        }
        else
        {
            offset = no_bits ? offset : placed->offset + placed->size;
            address = placed->address + placed->size;
        }
    }
    ExecutableSegmentLayout* last = &layout->segments[segment];
    last->file_size = layout->sections[EXECUTABLE_SECTION_BSS].offset - last->offset;
    last->memory_size = address - last->address;
    // RELRO's memory runs to its page's end, so that PT_GNU_RELRO protects all of it.
    ExecutableSegmentLayout* relro = &layout->segments[EXECUTABLE_SEGMENT_RELRO];
    relro->memory_size = executable_align(relro->address + relro->memory_size, BUSTER_EXECUTABLE_PAGE_BYTES) - relro->address;
    for (uint32_t loaded = 0; loaded < EXECUTABLE_SEGMENTS; loaded += 1)
    {
        const uint64_t end = layout->segments[loaded].address + layout->segments[loaded].memory_size;
        layout->image_size = layout->has_segment[loaded] && end > layout->image_size ? end : layout->image_size;
    }

    // The TLS segment: .tdata's start, and its size with .tbss's.
    const ExecutableSectionLayout* tdata = &layout->sections[EXECUTABLE_SECTION_TDATA];
    const ExecutableSectionLayout* tbss = &layout->sections[EXECUTABLE_SECTION_TBSS];
    layout->tls_size = tbss->size != 0 ? tbss->address + tbss->size - tdata->address : tdata->size;
    layout->tls_block = executable_align(layout->tls_size, layout->tls_alignment);
    const bool tls_fits = layout->tls_block <= BUSTER_EXECUTABLE_MAX_TLS_BYTES;

    // The pieces in file order: the headers, then before each class's buckets the linker's sections that come before
    // them, each bucket's start being its class's start in its section and then the buckets before it. The linker's
    // sections, in section order, come before classes that don't decrease, so one pass over the sections goes along with
    // the classes (passing the sections of atoms, which have no pieces of the linker's).
    uint64_t piece = 1;
    uint32_t linker = 0;
    for (uint8_t class = 1; class < BUSTER_LINK_CLASS_COUNT; class += 1)
    {
        while (linker < EXECUTABLE_SECTION_COUNT && (executable_linker_before[linker] == BUSTER_LINK_CLASS_COUNT || executable_linker_before[linker] <= class))
        {
            const bool here = executable_linker_before[linker] == class && layout->sections[linker].size != 0;
            layout->section_piece[linker] = here ? piece : layout->section_piece[linker];
            piece += here ? 1 : 0;
            linker += 1;
        }
        const uint8_t section = executable_class_section(class, crt);
        uint64_t cursor = section != EXECUTABLE_SECTION_COUNT ? layout->sections[section].address + (class == BUSTER_LINK_CLASS_STRINGS ? strings_start : 0) : 0;
        if (class == BUSTER_LINK_CLASS_INIT_ARRAY || class == BUSTER_LINK_CLASS_FINI_ARRAY)
        {
            const uint32_t array = executable_array_index(class);
            layout->prioritized_address[array] = cursor;
            layout->prioritized_piece[array] = piece;
            cursor += prioritized_bytes[array];
            piece += prioritized_pieces[array];
        }
        for (uint32_t log2 = BUSTER_EXECUTABLE_PAGE_LOG2 + 1; log2-- > 0;)
        {
            const uint32_t bucket = executable_bucket(class, log2);
            cursor = bytes[bucket] != 0 ? executable_align(cursor, executable_class_is_ordered(class, crt) ? 1ull << ordered_log2[class] : 1ull << log2) : cursor;
            layout->bucket_address[bucket] = cursor;
            layout->bucket_piece[bucket] = piece;
            cursor += bytes[bucket];
            piece += pieces[bucket];
        }
    }
    layout->got_slots = got_slots;
    // .shstrtab: a NUL, then the names of the sections with a size and its own, each with its NUL.
    layout->shstrtab_size = 1 + sizeof(".shstrtab");
    layout->section_header_count = 2;
    for (uint32_t section = 0; section < EXECUTABLE_SECTION_COUNT; section += 1)
    {
        const bool present = layout->sections[section].size != 0;
        layout->section_index[section] = present ? layout->section_header_count - 1 : 0;
        layout->section_header_count += present ? 1 : 0;
        layout->shstrtab_size += present ? strlen(executable_section_name(layout, section)) + 1 : 0;
    }
    layout->shstrtab_offset = offset;
    layout->section_headers_offset = executable_align(offset + layout->shstrtab_size, 8);
    layout->file_size = layout->section_headers_offset + layout->section_header_count * sizeof(Elf64_Shdr);
    layout->piece_count = piece + 2;
    layout->chunk_count = (layout->file_size + BUSTER_EXECUTABLE_CHUNK_BYTES - 1) / BUSTER_EXECUTABLE_CHUNK_BYTES;
    return layout->base + layout->image_size <= BUSTER_EXECUTABLE_MAX_ADDRESS && tls_fits;
}

// The file offset of an image offset in a section: sections keep their offset's place in a page.
static uint64_t executable_file_offset(const ExecutableLayout* layout, uint32_t section, uint64_t address)
{
    return layout->sections[section].offset + (address - layout->sections[section].address);
}

// ---------------------------------------------------------------------------------------------------------------------
// Headers

// The bytes of the linker's own pieces: the ELF header and program headers, the build ID's note, the section headers'
// string table and the section headers.
typedef struct ExecutableHeaders ExecutableHeaders;
struct ExecutableHeaders
{
    uint8_t headers[sizeof(Elf64_Ehdr) + BUSTER_EXECUTABLE_MAX_PROGRAM_HEADERS * sizeof(Elf64_Phdr)];
    uint8_t note[BUSTER_EXECUTABLE_NOTE_BYTES];
    char shstrtab[BUSTER_EXECUTABLE_SHSTRTAB_BYTES];
    Elf64_Shdr section_headers[BUSTER_EXECUTABLE_MAX_SECTION_HEADERS];
};

static Elf64_Phdr executable_program_header(const ExecutableLayout* layout, uint32_t type, uint32_t flags, uint64_t offset, uint64_t address, uint64_t file_size, uint64_t memory_size,
                                            uint64_t alignment)
{
    Elf64_Phdr result = {
        .p_type = type,
        .p_flags = flags,
        .p_offset = offset,
        .p_vaddr = layout->base + address,
        .p_paddr = layout->base + address,
        .p_filesz = file_size,
        .p_memsz = memory_size,
        .p_align = alignment,
    };
    return result;
}

// The headers, in lld's order of program headers: PT_PHDR, a dynamic PIE's PT_INTERP, the PT_LOADs, PT_TLS, a PIE's
// PT_DYNAMIC, PT_GNU_RELRO, PT_GNU_EH_FRAME, PT_GNU_STACK (not executable) and PT_NOTE (the ABI note and the build ID's,
// which follow each other). The note's hash stays zero until the build ID is known.
static void executable_write_headers(ExecutableHeaders* headers, const ExecutableLayout* layout, uint64_t entry)
{
    memset(headers, 0, sizeof(*headers));
    const Elf64_Ehdr header = {
        .e_ident = {
            ELFMAG0,
            ELFMAG1,
            ELFMAG2,
            ELFMAG3,
            ELFCLASS64,
            ELFDATA2LSB,
            EV_CURRENT,
            ELFOSABI_NONE,
        },
        .e_type = layout->dynamic ? ET_DYN : ET_EXEC,
        .e_machine = EM_X86_64,
        .e_version = EV_CURRENT,
        .e_entry = layout->base + entry,
        .e_phoff = sizeof(Elf64_Ehdr),
        .e_shoff = layout->section_headers_offset,
        .e_flags = 0,
        .e_ehsize = sizeof(Elf64_Ehdr),
        .e_phentsize = sizeof(Elf64_Phdr),
        .e_phnum = (uint16_t)layout->program_headers,
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_shnum = (uint16_t)layout->section_header_count,
        .e_shstrndx = (uint16_t)(layout->section_header_count - 1),
    };
    memcpy(headers->headers, &header, sizeof(header));

    static const uint32_t load_flags[EXECUTABLE_SEGMENTS] = {
        PF_R,
        PF_R | PF_X,
        PF_R | PF_W,
        PF_R | PF_W,
    };
    Elf64_Phdr program[BUSTER_EXECUTABLE_MAX_PROGRAM_HEADERS];
    uint32_t count = 0;
    const uint64_t program_bytes = layout->program_headers * sizeof(Elf64_Phdr);
    program[count] = executable_program_header(layout, PT_PHDR, PF_R, sizeof(Elf64_Ehdr), sizeof(Elf64_Ehdr), program_bytes, program_bytes, 8);
    count += 1;
    const ExecutableSectionLayout* interp = &layout->sections[EXECUTABLE_SECTION_INTERP];
    if (interp->size != 0)
    {
        program[count] = executable_program_header(layout, PT_INTERP, PF_R, interp->offset, interp->address, interp->size, interp->size, 1);
        count += 1;
    }
    for (uint32_t segment = 0; segment < EXECUTABLE_SEGMENTS; segment += 1)
    {
        const ExecutableSegmentLayout* placed = &layout->segments[segment];
        if (layout->has_segment[segment])
        {
            program[count] = executable_program_header(layout, PT_LOAD, load_flags[segment], placed->offset, placed->address, placed->file_size, placed->memory_size,
                                                       BUSTER_EXECUTABLE_PAGE_BYTES);
            count += 1;
        }
    }
    const ExecutableSectionLayout* tdata = &layout->sections[EXECUTABLE_SECTION_TDATA];
    if (layout->tls_size != 0)
    {
        program[count] = executable_program_header(layout, PT_TLS, PF_R, tdata->offset, tdata->address, tdata->size, layout->tls_size, layout->tls_alignment);
        count += 1;
    }
    const ExecutableSectionLayout* dynamic = &layout->sections[EXECUTABLE_SECTION_DYNAMIC];
    if (layout->dynamic)
    {
        program[count] = executable_program_header(layout, PT_DYNAMIC, PF_R | PF_W, dynamic->offset, dynamic->address, dynamic->size, dynamic->size, 8);
        count += 1;
    }
    if (layout->has_segment[EXECUTABLE_SEGMENT_RELRO])
    {
        const ExecutableSegmentLayout* relro = &layout->segments[EXECUTABLE_SEGMENT_RELRO];
        program[count] = executable_program_header(layout, PT_GNU_RELRO, PF_R, relro->offset, relro->address, relro->file_size, relro->memory_size, 1);
        count += 1;
    }
    const ExecutableSectionLayout* eh_frame_hdr = &layout->sections[EXECUTABLE_SECTION_EH_FRAME_HDR];
    if (eh_frame_hdr->size != 0)
    {
        program[count] = executable_program_header(layout, PT_GNU_EH_FRAME, PF_R, eh_frame_hdr->offset, eh_frame_hdr->address, eh_frame_hdr->size, eh_frame_hdr->size, 4);
        count += 1;
    }
    program[count] = (Elf64_Phdr){
        .p_type = PT_GNU_STACK,
        .p_flags = PF_R | PF_W,
    };
    count += 1;
    // The ABI note, when there is one, ends where the build ID's begins: both are 4-aligned and their sizes multiples of 4.
    const ExecutableSectionLayout* abi_note = &layout->sections[EXECUTABLE_SECTION_ABI_NOTE];
    const ExecutableSectionLayout* note = &layout->sections[EXECUTABLE_SECTION_BUILD_ID];
    const ExecutableSectionLayout* first_note = abi_note->size != 0 ? abi_note : note;
    program[count] = executable_program_header(layout, PT_NOTE, PF_R, first_note->offset, first_note->address, note->offset + note->size - first_note->offset,
                                               note->offset + note->size - first_note->offset, 4);
    count += 1;
    memcpy(headers->headers + sizeof(Elf64_Ehdr), program, count * sizeof(Elf64_Phdr));

    const Elf64_Nhdr note_header = {
        .n_namesz = 4,
        .n_descsz = BUSTER_EXECUTABLE_BUILD_ID_BYTES,
        .n_type = NT_GNU_BUILD_ID,
    };
    memcpy(headers->note, &note_header, sizeof(note_header));
    memcpy(headers->note + sizeof(note_header), "GNU", 4);

    // The section headers: the null one, every section with a size, and .shstrtab, whose names it holds. .dynsym's info
    // is its first global symbol's index, and .gnu.version_r's its one library's entry.
    uint32_t name = 1;
    uint32_t index = 1;
    for (uint32_t section = 0; section < EXECUTABLE_SECTION_COUNT; section += 1)
    {
        const ExecutableSectionLayout* placed = &layout->sections[section];
        if (placed->size != 0)
        {
            const char* section_name = executable_section_name(layout, section);
            const uint32_t length = (uint32_t)strlen(section_name);
            memcpy(headers->shstrtab + name, section_name, length + 1);
            const uint8_t linked = executable_section_links[section];
            headers->section_headers[index] = (Elf64_Shdr){
                .sh_name = name,
                .sh_type = executable_section_types[section],
                .sh_flags = executable_section_flags[section],
                .sh_addr = layout->base + placed->address,
                .sh_offset = placed->offset,
                .sh_size = placed->size,
                .sh_link = linked != 0 ? layout->section_index[linked] : 0,
                .sh_info = section == EXECUTABLE_SECTION_DYNSYM || section == EXECUTABLE_SECTION_VERNEED ? 1
                           : section == EXECUTABLE_SECTION_RELA_IPLT                                    ? layout->section_index[EXECUTABLE_SECTION_GOT]
                                                                                                        : 0,
                .sh_addralign = placed->alignment,
                .sh_entsize = executable_section_entry_bytes[section],
            };
            name += length + 1;
            index += 1;
        }
    }
    const char shstrtab_name[] = ".shstrtab";
    memcpy(headers->shstrtab + name, shstrtab_name, sizeof(shstrtab_name));
    headers->section_headers[index] = (Elf64_Shdr){
        .sh_name = name,
        .sh_type = SHT_STRTAB,
        .sh_offset = layout->shstrtab_offset,
        .sh_size = layout->shstrtab_size,
        .sh_addralign = 1,
    };
}

// ---------------------------------------------------------------------------------------------------------------------
// Chunks

// The first piece that ends after `offset`, or piece_count: pieces are in file order and don't overlap.
static uint64_t executable_piece_after(const ExecutablePiece* pieces, uint64_t piece_count, uint64_t offset)
{
    uint64_t low = 0;
    uint64_t high = piece_count;
    while (low < high)
    {
        const uint64_t middle = low + (high - low) / 2;
        if (pieces[middle].offset + pieces[middle].size > offset)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    return low;
}

// The first of an atom's fixups [begin, end) whose field starts at or after `offset` in the atom.
static FixupId executable_fixup_at(const uint32_t* offsets, FixupId begin, FixupId end, uint64_t offset)
{
    FixupId low = begin;
    FixupId high = end;
    while (low < high)
    {
        const FixupId middle = low + (high - low) / 2;
        if (offsets[middle] >= offset)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    return low;
}

// The bytes of an atom that fixup `fixup` (of the atom's fixups up to `end`) patches, [*start, *finish): its field, or for
// a general- or local-dynamic TLS access, the whole sequence its relaxation rewrites, from its lea (4 or 3 bytes before
// its field) to the end of its call's field, the next fixup's (executable_relax_tls); and in a static PIE (`relax`),
// for a CALL_GOT or LOAD_GOT, the 2 bytes before its field too, which its relaxation may rewrite (executable_relax_got).
static void executable_fixup_extent(const Link* link, FixupId fixup, FixupId end, bool relax, uint64_t* start, uint64_t* finish)
{
    const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
    const uint64_t offset = link->fixups.offset[fixup];
    const bool tls_access = kind == BUSTER_LINK_FIXUP_TLSGD || kind == BUSTER_LINK_FIXUP_TLSLD;
    const bool relaxing = relax && (kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT);
    const uint64_t before = tls_access ? (kind == BUSTER_LINK_FIXUP_TLSGD ? 4 : 3) : relaxing ? 2 : 0;
    const uint64_t call_end = tls_access && fixup + 1 < end ? (uint64_t)link->fixups.offset[fixup + 1] + 4 : offset + 4;
    *start = offset >= before ? offset - before : offset;
    *finish = tls_access ? (call_end > offset + 4 ? call_end : offset + 4) : offset + link_fixup_kinds[kind].width;
}

// Where the chunk boundary for `nominal` (a multiple of BUSTER_EXECUTABLE_CHUNK_BYTES) is: there, unless it falls
// inside an atom. An atom of at most a chunk ends the chunk at its end; a larger one is cut there, unless what a fixup
// patches crosses it (executable_fixup_extent: the field before it, or a TLS access's sequence or, with `relax`, a
// relaxing load's or call's instruction, which may start before its field), and then at that end. No field crosses an
// atom's end, and the linker's pieces have none.
static uint64_t executable_chunk_boundary(const Link* link, const ExecutablePiece* pieces, uint64_t piece_count, uint64_t nominal, bool relax)
{
    const uint64_t index = executable_piece_after(pieces, piece_count, nominal);
    uint64_t result = nominal;
    if (index < piece_count && pieces[index].atom != 0 && pieces[index].offset < nominal)
    {
        const ExecutablePiece* piece = &pieces[index];
        const AtomId atom = piece->atom;
        if (piece->size <= BUSTER_EXECUTABLE_CHUNK_BYTES)
        {
            result = piece->offset + piece->size;
        }
        else
        {
            // The field before the cut, if any: the last one that starts before it; and the one after it, whose TLS
            // sequence may start before it.
            const uint64_t cut = nominal - piece->offset;
            const FixupId begin = link->atoms.fixup_begin[atom];
            const FixupId end = begin + link->atoms.fixup_count[atom];
            const FixupId after = executable_fixup_at(link->fixups.offset, begin, end, cut);
            uint64_t start = 0;
            uint64_t finish = 0;
            if (after > begin)
            {
                executable_fixup_extent(link, after - 1, end, relax, &start, &finish);
                result = finish > cut ? piece->offset + finish : nominal;
            }
            if (after < end)
            {
                executable_fixup_extent(link, after, end, relax, &start, &finish);
                result = start < cut && finish > result - piece->offset ? piece->offset + finish : result;
            }
        }
    }
    return result;
}

// Patches the fixups whose fields are in [begin, end) of the file. Returns the smallest fixup whose value doesn't fit,
// or 0.
static FixupId executable_patch_chunk(const Link* link, const ExecutablePatch* patch, const ExecutablePiece* pieces, uint64_t piece_count, uint64_t begin, uint64_t end)
{
    FixupId result = 0;
    for (uint64_t i = executable_piece_after(pieces, piece_count, begin); i < piece_count && pieces[i].offset < end; i += 1)
    {
        const ExecutablePiece* piece = &pieces[i];
        const AtomId atom = piece->atom;
        if (atom != 0 && link->atoms.fixup_count[atom] != 0)
        {
            FixupId first = link->atoms.fixup_begin[atom];
            FixupId last = first + link->atoms.fixup_count[atom];
            // An atom cut by the chunk's boundaries: the fixups whose fields start in the chunk, which end there too.
            if (piece->offset < begin)
            {
                first = executable_fixup_at(link->fixups.offset, first, last, begin - piece->offset);
            }
            if (piece->offset + piece->size > end)
            {
                last = executable_fixup_at(link->fixups.offset, first, last, end - piece->offset);
            }
            const FixupId failed = executable_patch_range(patch, link->atoms.bytes[atom], (uint32_t)link->atoms.address[atom], first, last);
            // The smallest, which file order doesn't give: buckets put larger alignments first.
            result = failed != 0 && (result == 0 || failed < result) ? failed : result;
        }
    }
    return result;
}

// Fill for the gaps between pieces: int3 in .text, zeros elsewhere.
#define BUSTER_EXECUTABLE_FILL_BYTES 4096
static const uint8_t executable_zeros[BUSTER_EXECUTABLE_FILL_BYTES] = { 0 };
static const uint8_t executable_int3s[BUSTER_EXECUTABLE_FILL_BYTES] = { [0 ... BUSTER_EXECUTABLE_FILL_BYTES - 1] = BUSTER_EXECUTABLE_INT3 };

// Adds a run of `size` bytes from `bytes` to runs[*count], merged with the run before it when they are adjacent in
// memory.
static void executable_add_run(struct iovec* runs, uint64_t* count, const uint8_t* bytes, uint64_t size)
{
    struct iovec* previous = *count != 0 ? &runs[*count - 1] : 0;
    if (previous != 0 && (const uint8_t*)previous->iov_base + previous->iov_len == bytes)
    {
        previous->iov_len += size;
    }
    else if (size != 0)
    {
        runs[*count] = (struct iovec){
            .iov_base = (void*)bytes,
            .iov_len = size,
        };
        *count += 1;
    }
}

static void executable_add_gap(struct iovec* runs, uint64_t* count, const ExecutableLayout* layout, uint64_t offset, uint64_t size)
{
    const ExecutableSectionLayout* text = &layout->sections[EXECUTABLE_SECTION_TEXT];
    const bool in_text = offset >= text->offset && offset < text->offset + text->size;
    const uint8_t* fill = in_text ? executable_int3s : executable_zeros;
    for (uint64_t done = 0; done < size; done += BUSTER_EXECUTABLE_FILL_BYTES)
    {
        const uint64_t part = size - done < BUSTER_EXECUTABLE_FILL_BYTES ? size - done : BUSTER_EXECUTABLE_FILL_BYTES;
        // Fill buffers repeat: a merge would read past one, so each part is a run of its own.
        runs[*count] = (struct iovec){
            .iov_base = (void*)fill,
            .iov_len = part,
        };
        *count += part != 0 ? 1 : 0;
    }
}

// The most runs that [begin, end) of the file can need: two per piece in it (its gap and its bytes), the gap after the
// last, and the gaps' parts past a fill buffer.
static uint64_t executable_run_capacity(const ExecutablePiece* pieces, uint64_t piece_count, uint64_t begin, uint64_t end)
{
    const uint64_t first = executable_piece_after(pieces, piece_count, begin);
    const uint64_t last = executable_piece_after(pieces, piece_count, end);
    return 2 * (last - first + 1) + 1 + (end - begin) / BUSTER_EXECUTABLE_FILL_BYTES;
}

// The bytes of [begin, end) of the file, as runs: the pieces' bytes and the gaps' fill, in file order. Returns how
// many; runs has room for executable_run_capacity of them.
static uint64_t executable_chunk_runs(const ExecutableLayout* layout, const ExecutablePiece* pieces, uint64_t piece_count, uint64_t begin, uint64_t end, struct iovec* runs)
{
    uint64_t count = 0;
    uint64_t cursor = begin;
    for (uint64_t i = executable_piece_after(pieces, piece_count, begin); i < piece_count && pieces[i].offset < end; i += 1)
    {
        const ExecutablePiece* piece = &pieces[i];
        const uint64_t from = piece->offset > begin ? piece->offset : begin;
        const uint64_t to = piece->offset + piece->size < end ? piece->offset + piece->size : end;
        executable_add_gap(runs, &count, layout, cursor, from - cursor);
        executable_add_run(runs, &count, piece->bytes + (from - piece->offset), to - from);
        cursor = to;
    }
    executable_add_gap(runs, &count, layout, cursor, end - cursor);
    return count;
}

// ---------------------------------------------------------------------------------------------------------------------
// Unwind tables
//
// .eh_frame holds its atoms' records one after another (executable_class_is_ordered), and .eh_frame_hdr indexes their
// FDEs, so that an unwinder looks a return address up in its table rather than walk the records: the LSB's format,
// which lld writes with --eh-frame-hdr. An atom's records must be ones the header can index: CIEs whose augmentation is
// 'z' and 'R', 'P', 'L' or 'S', with 'R''s FDE pointers pc-relative sdata4, as GCC and clang give every x86-64 one; and
// FDEs that point back to one of them in the same atom, each with its pc_begin a PC32 fixup's field, which gives the
// function's address (S + A). A zero length ends the records, as crtendS.o's terminator does. An atom with others fails
// the link (EXECUTABLE_ERROR_UNWIND), and so does one with an FDE whose function is farther from the header than an
// int32 reaches, which GNU ld and lld refuse too.
//   - The count, before the layout, by lanes that share the .eh_frame atoms by cost, their runs in id order
//     (executable_count_frames): each atom's FDEs from its fixups, 16 at a time, each a candidate 8 bytes before its
//     field, whose length and CIE pointer are loaded for all 16 before any is compared, since no candidate's place
//     depends on another's as a walk's next record does on its last (executable_eh_frame_fdes). An atom the candidates
//     don't describe (its records laid out otherwise, or other fixups among them) goes to the exact walk, record by
//     record (executable_eh_frame_walk), which is also the twin that ExecutableOptions.exact selects.
//   - The table, once every symbol's reference is known: each lane the entries of its share's FDEs, in .eh_frame's
//     order, an atom the vector path read from its fixups' columns alone (executable_eh_frame_entries), one the walk
//     read by the walk, and an atom with an entry outside int32 failing (executable_eh_frame_table); then lane 0 checks
//     that they are sorted by initial location, 16 at a time, as they are when the functions' addresses go in the order
//     of their FDEs, as in a link of a compiler's objects without cold parts (a static executable's libc has them, each
//     function's two FDEs together); only if they aren't does it sort them, which the exact twin always does
//     (executable_eh_frame_sort).

// .eh_frame_hdr's first 12 bytes: version 1; the encodings of .eh_frame's address (pc-relative sdata4), of the count
// (udata4) and of the table's entries (datarel sdata4: from the header's start); .eh_frame's address and the count.
// Then the table, two int32 per FDE: its function's initial location and its own address.
#define BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES 12
#define BUSTER_EXECUTABLE_EH_PE_PCREL_SDATA4 0x1b
#define BUSTER_EXECUTABLE_EH_PE_UDATA4 0x03
#define BUSTER_EXECUTABLE_EH_PE_DATAREL_SDATA4 0x3b
// The CIEs an atom's walk keeps, among which its FDEs' must be.
#define BUSTER_EXECUTABLE_EH_FRAME_CIES 8

static uint32_t executable_load32(const uint8_t* bytes)
{
    uint32_t result;
    memcpy(&result, bytes, sizeof(result));
    return result;
}

// Reads the LEB128 at *position, before `end`, past it: its value if `value` (unsigned, its first 64 bits). False if it
// runs past `end`.
static bool executable_leb128(const uint8_t* bytes, uint64_t end, uint64_t* position, uint64_t* value)
{
    uint64_t at = *position;
    uint64_t result = 0;
    uint32_t shift = 0;
    bool more = true;
    while (more && at < end)
    {
        result |= shift < 64 ? (uint64_t)(bytes[at] & 0x7f) << shift : 0;
        more = (bytes[at] & 0x80) != 0;
        shift += 7;
        at += 1;
    }
    *position = at;
    if (value != 0)
    {
        *value = result;
    }
    return !more;
}

// The bytes of a pointer of DWARF encoding `encoding`, by its low bits: absptr, udata2, udata4, udata8, sdata2, sdata4
// and sdata8 by their sizes, uleb128 and sleb128 as 0 (a LEB128's), and UINT32_MAX for the others.
static uint32_t executable_pointer_bytes(uint8_t encoding)
{
    static const uint32_t bytes[16] = { 8, 0, 2, 4, 8, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 0, 2, 4, 8, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    return bytes[encoding & 0x0f];
}

// Whether the record at `cie`, a CIE of `length` bytes past its length field, is one the header can index: version 1
// or 3, and an augmentation of 'z' then 'R', 'P', 'L' and 'S', 'R''s FDE pointers pc-relative sdata4. 'P''s personality
// pointer and 'L''s LSDA encoding are skipped within the augmentation data's length.
static bool executable_cie_indexable(const uint8_t* bytes, uint64_t cie, uint32_t length)
{
    const uint64_t end = cie + 4 + (uint64_t)length;
    uint64_t position = cie + 8;
    const uint8_t version = position < end ? bytes[position] : 0;
    position += 1;
    const uint64_t augmentation = position;
    while (position < end && bytes[position] != 0)
    {
        position += 1;
    }
    bool valid = (version == 1 || version == 3) && position < end && bytes[augmentation] == 'z';
    position += 1;
    // The code and data alignments, and the return address register: a byte in version 1, a LEB128 in 3.
    valid = valid && executable_leb128(bytes, end, &position, 0) && executable_leb128(bytes, end, &position, 0);
    valid = valid && (version == 3 ? executable_leb128(bytes, end, &position, 0) : position < end);
    position += version == 1 ? 1 : 0;
    uint64_t data_length = 0;
    valid = valid && executable_leb128(bytes, end, &position, &data_length) && data_length <= end - position;
    const uint64_t data_end = valid ? position + data_length : 0;
    bool encoded = false;
    for (uint64_t a = augmentation + 1; valid && bytes[a] != 0; a += 1)
    {
        const uint8_t letter = bytes[a];
        const uint8_t datum = position < data_end ? bytes[position] : 0;
        const uint32_t pointer = letter == 'P' ? executable_pointer_bytes(datum) : 0;
        valid = (letter == 'S' || position < data_end) && (letter == 'R' || letter == 'P' || letter == 'L' || letter == 'S') && pointer != UINT32_MAX;
        encoded = encoded || (letter == 'R' && datum == BUSTER_EXECUTABLE_EH_PE_PCREL_SDATA4);
        valid = valid && (letter != 'R' || datum == BUSTER_EXECUTABLE_EH_PE_PCREL_SDATA4);
        position += letter == 'S' ? 0 : 1;
        if (valid && letter == 'P' && pointer == 0)
        {
            valid = executable_leb128(bytes, data_end, &position, 0);
        }
        position += letter == 'P' ? pointer : 0;
    }
    return valid && encoded && position <= data_end;
}

// The exact walk of an .eh_frame atom's records, one after another (see this section's comment): how many FDEs it has,
// or UINT32_MAX if a record isn't one the header can index. With `entries`, each FDE's table entry from the first, for
// the header at image offset `header` and the atom at `address`, and UINT32_MAX also if one doesn't fit.
static uint32_t executable_eh_frame_walk(const Link* link, AtomId atom, int32_t* entries, uint64_t header, uint64_t address)
{
    const uint8_t* bytes = link->atoms.bytes[atom];
    const uint64_t size = link->atoms.size[atom];
    const LinkFixups* fixups = &link->fixups;
    FixupId fixup = link->atoms.fixup_begin[atom];
    const FixupId fixups_end = fixup + link->atoms.fixup_count[atom];
    uint64_t cies[BUSTER_EXECUTABLE_EH_FRAME_CIES];
    uint32_t cie_count = 0;
    uint64_t offset = 0;
    uint32_t count = 0;
    bool valid = bytes != 0 || size == 0;
    bool ended = false;
    while (valid && !ended && offset < size)
    {
        valid = size - offset >= 4;
        const uint32_t length = valid ? executable_load32(bytes + offset) : 0;
        ended = valid && length == 0;
        valid = valid && (ended || (length >= 4 && (uint64_t)length <= size - offset - 4));
        const uint32_t id = valid && !ended ? executable_load32(bytes + offset + 4) : 0;
        if (valid && !ended && id == 0)
        {
            valid = cie_count < BUSTER_EXECUTABLE_EH_FRAME_CIES && executable_cie_indexable(bytes, offset, length);
            cies[valid ? cie_count : 0] = offset;
            cie_count += valid ? 1 : 0;
        }
        else if (valid && !ended)
        {
            // Its CIE, from its pointer's field, among the ones met; and its pc_begin's fixup, the fixups being in
            // offset order.
            const uint64_t pointed = offset + 4 >= id ? offset + 4 - id : UINT64_MAX;
            bool known = false;
            for (uint32_t c = 0; c < cie_count; c += 1)
            {
                known = known || cies[c] == pointed;
            }
            while (fixup < fixups_end && fixups->offset[fixup] < offset + 8)
            {
                fixup += 1;
            }
            valid = known && length >= 12 && fixup < fixups_end && fixups->offset[fixup] == offset + 8 &&
                    (fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_PC32;
            if (valid && entries != 0)
            {
                // The function's address, S + A, from the header, which must fit the entry.
                const int64_t reference = link->symbols.reference[fixups->target[fixup]];
                int64_t initial = 0;
                valid = !__builtin_add_overflow(reference - (int64_t)header, link_fixup_addend(link, fixup), &initial) && initial >= INT32_MIN && initial <= INT32_MAX;
                entries[2 * (uint64_t)count] = (int32_t)initial;
                entries[2 * (uint64_t)count + 1] = (int32_t)(address + offset - header);
            }
            count += valid ? 1 : 0;
        }
        offset += valid && !ended ? 4 + (uint64_t)length : 0;
    }
    return valid ? count : UINT32_MAX;
}

// The FDEs of an .eh_frame atom that is a CIE then FDEs pointing back to it, and perhaps a terminator, each FDE with
// its pc_begin a PC32 fixup's field (of an addend in the column) and nothing else fixed up: their count, or UINT32_MAX
// for an atom that isn't, which the exact walk then reads. 16 fixups at a time, each a candidate FDE 8 bytes before its
// field: the candidates' lengths and CIE pointers are loaded, 8 bytes each by scalar loads that don't wait on each other
// (gathers cost more on Zen 5), then compared 16 at a time: each must start where the one before it ends (the first
// where the CIE does, the one before a group's first in the last group's last lane), point back to the CIE, and be long
// enough for pc_begin and the range; and the last must end at the atom's end or its terminator's start, so none ends
// past it: the one after an FDE that did would start past its field's candidate.
static uint32_t executable_eh_frame_fdes(const Link* link, AtomId atom)
{
    const uint8_t* bytes = link->atoms.bytes[atom];
    const uint64_t size = link->atoms.size[atom];
    const LinkFixups* fixups = &link->fixups;
    const FixupId first = link->atoms.fixup_begin[atom];
    const uint32_t count = link->atoms.fixup_count[atom];
    const uint32_t cie_length = bytes != 0 && size >= 8 ? executable_load32(bytes) : 0;
    bool valid = bytes != 0 && size >= 8 && size <= BUSTER_EXECUTABLE_MAX_ATOM_BYTES && executable_load32(bytes + 4) == 0 && cie_length >= 4 &&
                 (uint64_t)cie_length <= size - 4 && executable_cie_indexable(bytes, 0, cie_length);
    const __m512i size_vector = _mm512_set1_epi32((int32_t)size);
    const __m512i evens = _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
    const __m512i odds = _mm512_set_epi32(31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1);
    // The last group's ends, whose last lane the next group's first FDE must start at.
    __m512i last_ends = _mm512_set1_epi32((int32_t)(cie_length + 4));
    uint32_t last = 15;
    for (uint32_t i = 0; valid && i < count; i += 16)
    {
        const uint32_t n = count - i < 16 ? count - i : 16;
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, n);
        const __m512i offsets = _mm512_maskz_loadu_epi32(lanes, fixups->offset + first + i);
        const __m512i kinds = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, fixups->kind + first + i));
        const __m512i starts = _mm512_sub_epi32(offsets, _mm512_set1_epi32(8));
        // Each candidate's 12 bytes, its length, CIE pointer and pc_begin, within the atom.
        const __mmask16 inside = _mm512_mask_cmpge_epu32_mask(lanes, offsets, _mm512_set1_epi32(8)) &
                                 _mm512_mask_cmple_epu32_mask(lanes, _mm512_add_epi32(offsets, _mm512_set1_epi32(4)), size_vector);
        valid = inside == lanes && _mm512_mask_cmpneq_epi32_mask(lanes, kinds, _mm512_set1_epi32(BUSTER_LINK_FIXUP_PC32)) == 0;
        uint32_t start_of[16];
        uint64_t head_of[16] = { 0 };
        _mm512_storeu_si512(start_of, starts);
        for (uint32_t j = 0; valid && j < n; j += 1)
        {
            memcpy(&head_of[j], bytes + start_of[j], sizeof(head_of[j]));
        }
        // The lengths are the heads' low halves, the CIE pointers their high ones.
        const __m512i heads_low = _mm512_loadu_si512(head_of);
        const __m512i heads_high = _mm512_loadu_si512(head_of + 8);
        const __m512i lengths = _mm512_permutex2var_epi32(heads_low, evens, heads_high);
        const __m512i pointers = _mm512_permutex2var_epi32(heads_low, odds, heads_high);
        // Lengths within the atom first, so that the ends fit 32 bits.
        const __mmask16 sized = _mm512_mask_cmpge_epu32_mask(lanes, lengths, _mm512_set1_epi32(12)) & _mm512_mask_cmple_epu32_mask(lanes, lengths, size_vector);
        const __m512i ends = _mm512_add_epi32(_mm512_add_epi32(starts, _mm512_set1_epi32(4)), lengths);
        const __m512i before = _mm512_alignr_epi32(ends, last_ends, 15);
        const __mmask16 placed = _mm512_mask_cmpeq_epi32_mask(lanes, starts, before);
        const __mmask16 pointing = _mm512_mask_cmpeq_epi32_mask(lanes, pointers, _mm512_add_epi32(starts, _mm512_set1_epi32(4)));
        valid = valid && (sized & placed & pointing) == lanes;
        last_ends = ends;
        last = n - 1;
    }
    const uint64_t last_end = (uint32_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(_mm512_permutexvar_epi32(_mm512_set1_epi32((int32_t)last), last_ends)));
    valid = valid && (last_end == size || (last_end + 4 == size && executable_load32(bytes + size - 4) == 0));
    return valid ? count : UINT32_MAX;
}

// The table entries of an atom that executable_eh_frame_fdes read, whose fixups are its FDEs' pc_begins, each 8 bytes
// past its FDE's start: from the fixups' columns alone, 16 at a time, each its target's reference plus its addend, and
// its FDE's address, from the header at image offset `header` for the atom at `address`. False if one doesn't fit,
// which is checked in 64 bits: the reference less the header, offset by 2^31, plus the addend must be below 2^32.
static bool executable_eh_frame_entries(const Link* link, AtomId atom, int32_t* entries, uint64_t header, uint64_t address)
{
    const LinkFixups* fixups = &link->fixups;
    const FixupId first = link->atoms.fixup_begin[atom];
    const uint32_t count = link->atoms.fixup_count[atom];
    const __m512i bias = _mm512_set1_epi64(0x80000000ll - (int64_t)header);
    const __m512i limit = _mm512_set1_epi64(1ll << 32);
    const __m512i from_header = _mm512_set1_epi32((int32_t)(address - 8 - header));
    const __m512i low_order = _mm512_set_epi32(23, 7, 22, 6, 21, 5, 20, 4, 19, 3, 18, 2, 17, 1, 16, 0);
    const __m512i high_order = _mm512_set_epi32(31, 15, 30, 14, 29, 13, 28, 12, 27, 11, 26, 10, 25, 9, 24, 8);
    bool fits = true;
    for (uint32_t i = 0; fits && i < count; i += 16)
    {
        const uint32_t n = count - i < 16 ? count - i : 16;
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, n);
        const __m512i offsets = _mm512_maskz_loadu_epi32(lanes, fixups->offset + first + i);
        const __m512i targets = _mm512_maskz_loadu_epi32(lanes, fixups->target + first + i);
        const __m512i addends = _mm512_maskz_loadu_epi32(lanes, fixups->addend + first + i);
        uint32_t target_of[16];
        uint32_t reference_of[16] = { 0 };
        _mm512_storeu_si512(target_of, targets);
        for (uint32_t j = 0; j < n; j += 1)
        {
            reference_of[j] = link->symbols.reference[target_of[j]];
        }
        const __m512i references = _mm512_loadu_si512(reference_of);
        const __m512i wide_low = _mm512_add_epi64(_mm512_add_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(references)), _mm512_cvtepi32_epi64(_mm512_castsi512_si256(addends))), bias);
        const __m512i wide_high =
            _mm512_add_epi64(_mm512_add_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(references, 1)), _mm512_cvtepi32_epi64(_mm512_extracti64x4_epi64(addends, 1))), bias);
        const __mmask16 outside = (__mmask16)(_mm512_cmpge_epu64_mask(wide_low, limit) | (uint32_t)_mm512_cmpge_epu64_mask(wide_high, limit) << 8);
        fits = (outside & lanes) == 0;
        const __m512i initial = _mm512_sub_epi32(_mm512_add_epi32(references, addends), _mm512_set1_epi32((int32_t)header));
        const __m512i fde = _mm512_add_epi32(offsets, from_header);
        const __m512i low = _mm512_permutex2var_epi32(initial, low_order, fde);
        const __m512i high = _mm512_permutex2var_epi32(initial, high_order, fde);
        const uint32_t words = 2 * n;
        int32_t* place = entries + 2 * (uint64_t)i;
        _mm512_mask_storeu_epi32(place, (__mmask16)_bzhi_u32(0xffff, words), low);
        _mm512_mask_storeu_epi32(place + 16, (__mmask16)_bzhi_u32(0xffff, words > 16 ? words - 16 : 0), high);
    }
    return fits;
}

// An .eh_frame atom's FDEs: by the vector path, or if it can't read the atom, or with `exact`, by the walk, which
// *walked then says.
static uint32_t executable_eh_frame(const Link* link, AtomId atom, bool exact, bool* walked)
{
    const uint32_t vector = exact ? UINT32_MAX : executable_eh_frame_fdes(link, atom);
    *walked = vector == UINT32_MAX;
    return vector != UINT32_MAX ? vector : executable_eh_frame_walk(link, atom, 0, 0, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Writing

// A lane's first failure in a phase: the error, and the smallest atom, symbol or fixup it was about.
typedef struct ExecutableLaneError ExecutableLaneError;
struct ExecutableLaneError
{
    // On a line of its own, since each lane writes its own.
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) ExecutableError error;
    AtomId atom;
    SymbolId symbol;
    FixupId fixup;
    // For EXECUTABLE_ERROR_FILE: the write's errno.
    int system_error;
};

// A dynamic PIE's counts of one lane, each from its range of what it says: the GOT's words' stubs, and their slots that
// the loader relocates, RELATIVE and otherwise; the symbols' .dynsym entries (the SYMBOLIC ones, all undefined) and
// their names' bytes with NULs; the fixups' ABS64 ones, their ABS32 and ABS32S ones, and the others to a dynamic symbol
// or address 0 that can't reach it (executable_check_direct); the ABS64 fixups of the atoms the loader writes, and of
// those the ones .rela.dyn relocates, RELATIVE and otherwise; the relocations .relr.dyn holds (the GOT's and the
// fixups') with its words, and the GOT's words among them; and once the layout is known, the RELR words of its range of
// the pieces before the GOT's piece and after it. A static executable with the C runtime has the stubs too, its IFUNCs',
// and its IRELATIVE relocations, the slots of its IFUNCs in the lane's range of the GOT's words.
typedef struct ExecutableDynamicCounts ExecutableDynamicCounts;
struct ExecutableDynamicCounts
{
    // On a line of its own, since each lane writes its own.
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint64_t stubs;
    uint64_t irelatives;
    // A static executable's general- and local-dynamic TLS accesses (TLSGD and TLSLD fixups), and its fixups to
    // __tls_get_addr, which must be their calls.
    uint64_t tls_accesses;
    uint64_t relaxed_calls;
    uint64_t got_relative;
    uint64_t got_symbolic;
    uint64_t symbols;
    uint64_t name_bytes;
    uint64_t abs64_fixups;
    uint64_t absolute32;
    uint64_t refused;
    uint64_t abs64_written;
    uint64_t abs64_relative;
    uint64_t abs64_symbolic;
    uint64_t relr_relocations;
    uint64_t relr_words;
    uint64_t got_relr_words;
    uint64_t relr_before_got;
    uint64_t relr_after_got;
};

// What every lane computes alike of a dynamic PIE from the lanes' counts, before the layout: the totals, the versions
// the imports need, and the sizes of the linker's sections.
typedef struct ExecutableDynamicTotals ExecutableDynamicTotals;
struct ExecutableDynamicTotals
{
    uint64_t stubs;
    // .dynsym's undefined entries (after the null one) and its exports, and .dynstr's offsets of the exports' names,
    // the library's and the versions'.
    uint64_t undefined;
    uint64_t exports;
    uint64_t export_names;
    // Each needed library's name in .dynstr.
    uint64_t needed_names[BUSTER_EXECUTABLE_LIBRARIES];
    uint64_t version_names;
    uint64_t got_relative;
    uint64_t got_symbolic;
    uint64_t abs64_relative;
    uint64_t abs64_symbolic;
    uint64_t relr_relocations;
    uint64_t relr_words;
    // With the static runtime, the IRELATIVE relocations: a static executable's, in .rela.iplt, and a static PIE's, in
    // .rela.dyn after the others.
    uint64_t irelatives;
    // The needed versions, each library's, and the libraries with one (.gnu.version_r's entries).
    uint64_t versions;
    uint64_t library_versions[BUSTER_EXECUTABLE_LIBRARIES];
    uint64_t verneed_files;
    uint64_t buckets;
    uint64_t bloom_words;
    uint32_t dynamic_entries;
    // Whether each library is needed (a dynamic PIE's DT_NEEDED entries; see ExecutableDynamic.needed).
    bool needed[BUSTER_EXECUTABLE_LIBRARIES];
    bool has_init_array;
    bool has_fini_array;
    bool has_init;
    bool has_fini;
    uint64_t linker_bytes[EXECUTABLE_SECTION_COUNT];
};

// What the lanes share, which lane 0 allocates from its scratch arena and broadcasts. Each lane writes its own row of
// the per-lane arrays, and its own range of the others.
typedef struct ExecutableShared ExecutableShared;
struct ExecutableShared
{
    ExecutableCounts* counts;
    // Whether the passes go by their exact twins (ExecutableOptions.exact); whether the executable links the C runtime
    // (a PIE, or a static executable with it: ExecutableLayout.crt); whether it links the static runtime (a static
    // executable with the C runtime, or a static PIE: ExecutableLayout.runtime); whether symbols can be dynamic (a dynamic
    // PIE: its imports, and the weak references to nothing that a library may define); and whether the GOT's loads and
    // calls relax to the image's symbols (a static PIE: executable_relax_got). A PIE's (0 for a static executable): the
    // bitsets of the symbols by relocation class (the dynamic ones, SYMBOLIC, and the RELATIVE ones), words_per_lane
    // words, each lane writing its range of words; each lane's part of the filters; the lanes' counts; each lane's
    // bitset of the symbols that have stubs (words_per_lane words each, reduced into row 0 as the GOT's are); each lane's
    // bitset of the library's version indices that its imports need (version_words words of indices below version_limit
    // each), and lane 0's numbers for them in the executable (from 2, 0 for an index no import needs); and with RELR, the
    // RELR words of each atom the loader writes (executable_class_is_written; the others' are never read), with
    // BUSTER_EXECUTABLE_RELR_DENSE and BUSTER_EXECUTABLE_RELR_RELA. With the static runtime, the filters, the counts and
    // the stubs' bitsets too, for its IFUNCs, whose bitset (words_per_lane words) is ifunc_bits; and in a static PIE, the
    // bitset of the symbols that the GOT's loads and calls relax to, relaxable_bits: the RELATIVE ones but the IFUNCs.
    bool exact;
    bool crt;
    bool runtime;
    bool imports;
    bool relax;
    bool relr;
    // With the static runtime, the link's __tls_get_addr (ExecutableRuntime.tls_get_addr), or 0.
    SymbolId relaxed;
    uint64_t* ifunc_bits;
    uint64_t* dynamic_bits;
    uint64_t* relative_bits;
    uint64_t* relaxable_bits;
    // Each lane's part of the filters, filter_words words: a PIE's of the symbols outside the image
    // (BUSTER_EXECUTABLE_FILTER_WORDS words), and with the static runtime, from word ifunc_filter, its IFUNCs' by two
    // hashes (twice as many).
    uint32_t* filters;
    uint32_t filter_words;
    uint32_t ifunc_filter;
    uint32_t* relr_words;
    ExecutableDynamicCounts* dynamic_counts;
    uint64_t* stub_marks;
    uint64_t* versions_used;
    uint64_t version_words;
    uint32_t version_limit;
    uint16_t* version_numbers;
    // Once the layout is known, the bytes of the linker's sections (its pieces'): the note's and the GOT's, and a
    // dynamic PIE's others.
    uint8_t* section_bytes[EXECUTABLE_SECTION_COUNT];
    // The lanes' first failures, a row per phase: lane i's in phase p at [p * lanes + i]. A phase writes only its own
    // row, so a lane that has started the next phase never changes what another still reads of the last one.
    ExecutableLaneError* errors;
    // Each lane's bitset of the symbols that a GOT kind targets, words_per_lane words each; after the reduction, row 0
    // holds them all. Then each lane's count of them in its range of words, and its largest addend magnitude. In a static
    // PIE, the targets of the loads and calls that may relax are each lane's bitset of candidates (relax_marks), which
    // the reduction marks but for the relaxable ones.
    uint64_t* got_marks;
    uint64_t* relax_marks;
    uint64_t words_per_lane;
    uint64_t* got_counts;
    uint64_t* addend_magnitudes;
    // A static executable's: the lowest and highest reference of an absolute symbol in each lane's range of the symbols
    // (two per lane, INT64_MAX and INT64_MIN without one), which the patch's range proof takes in (a dynamic PIE refuses
    // the fixups it would be about: executable_refuses_absolute).
    int64_t* absolute_bounds;
    ExecutablePiece* pieces;
    // Allocated by lane 0 once the layout is known: the GOT's bytes, the chunks' boundaries (chunk_count + 1) and their
    // hashes, the parts' boundaries, and each lane's order of the parts.
    uint8_t* got;
    uint64_t* boundaries;
    uint8_t* hashes;
    uint64_t* part_boundaries;
    uint32_t* orders;
    // Whether the output is written in order, which lane 0 does at the end, rather than chunk by chunk.
    bool sequential;
    ExecutableHeaders headers;
};

static void executable_lane_fail(ExecutableLaneError* error, ExecutableError kind, AtomId atom, SymbolId symbol, FixupId fixup)
{
    if (error->error == EXECUTABLE_ERROR_NONE)
    {
        *error = (ExecutableLaneError){
            .error = kind,
            .atom = atom,
            .symbol = symbol,
            .fixup = fixup,
        };
    }
}

// This lane's error slot in a phase's row.
static ExecutableLaneError* executable_error(ExecutableShared* shared, ExecutablePhase phase, uint32_t lane, uint32_t lanes)
{
    return &shared->errors[(uint64_t)phase * lanes + lane];
}

// Whether any lane failed in a phase, after the sync that ends it: every lane gets the same answer, since no lane writes
// that row any more.
static bool executable_any_failed(const ExecutableShared* shared, ExecutablePhase phase, uint32_t lanes)
{
    bool result = false;
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        result = result || shared->errors[(uint64_t)phase * lanes + lane].error != EXECUTABLE_ERROR_NONE;
    }
    return result;
}

// Whether a symbol's name is `text`.
static bool executable_named(const Link* link, SymbolId symbol, const char* text)
{
    const NameId name = link->symbols.name[symbol];
    const uint64_t length = strlen(text);
    return name != 0 && link->names.length[name] == length && memcmp(link->names.bytes[name], text, length) == 0;
}

// Whether a symbol is thread-local: by its atom's class, or for a symbol without an atom (a weak one defined nowhere)
// by its type.
static bool executable_is_tls(const Link* link, SymbolId symbol)
{
    const AtomId atom = link->symbols.atom[symbol];
    return atom != 0 ? link_class_is_tls(link->atoms.class[atom]) : (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
}

// The names the linker defines (BUSTER_LINK_SOURCE_LINKER), each one's image offset in `values` (by its index here),
// as GNU ld and lld define them: the init and fini arrays' bounds and the GOT's _GLOBAL_OFFSET_TABLE_ (step 4's
// executables have them, and a dynamic PIE); .dynamic's address (_DYNAMIC), which binding gives the PIEs; and, which
// binding gives a static executable with the C runtime and a static PIE (runtime_linker_names), the ELF header's
// address and the image's start, the preinit array's bounds (where .init_array starts: there is no .preinit_array), the
// text's end (the RX segment's), the end of the sections with bytes in the file (lld's _edata) and .bss's start, and
// the image's end; and a static executable alone, .rela.iplt's IRELATIVE relocations' bounds.
static const char* const executable_linker_names[] = {
    "_GLOBAL_OFFSET_TABLE_",
    "__init_array_start",
    "__init_array_end",
    "__fini_array_start",
    "__fini_array_end",
    "_DYNAMIC",
    "__ehdr_start",
    "__executable_start",
    "__preinit_array_start",
    "__preinit_array_end",
    "__rela_iplt_start",
    "__rela_iplt_end",
    "_etext",
    "etext",
    "__etext",
    "_edata",
    "edata",
    "__bss_start",
    "_end",
    "end",
};

static void executable_linker_values(const ExecutableLayout* layout, uint64_t* values)
{
    const ExecutableSectionLayout* init = &layout->sections[EXECUTABLE_SECTION_INIT_ARRAY];
    const ExecutableSectionLayout* fini = &layout->sections[EXECUTABLE_SECTION_FINI_ARRAY];
    const ExecutableSectionLayout* irelative = &layout->sections[EXECUTABLE_SECTION_RELA_IPLT];
    const ExecutableSegmentLayout* text = &layout->segments[EXECUTABLE_SEGMENT_RX];
    uint64_t data_end = 0;
    for (uint32_t section = 0; section < EXECUTABLE_SECTION_COUNT; section += 1)
    {
        const ExecutableSectionLayout* placed = &layout->sections[section];
        const bool bytes = placed->size != 0 && executable_section_types[section] != SHT_NOBITS;
        data_end = bytes && placed->address + placed->size > data_end ? placed->address + placed->size : data_end;
    }
    const uint64_t text_end = layout->has_segment[EXECUTABLE_SEGMENT_RX] ? text->address + text->memory_size : layout->header_bytes;
    const uint64_t computed[] = {
        layout->sections[EXECUTABLE_SECTION_GOT].address,
        init->address,
        init->address + init->size,
        fini->address,
        fini->address + fini->size,
        layout->sections[EXECUTABLE_SECTION_DYNAMIC].address,
        0,
        0,
        init->address,
        init->address,
        irelative->address,
        irelative->address + irelative->size,
        text_end,
        text_end,
        text_end,
        data_end,
        data_end,
        layout->sections[EXECUTABLE_SECTION_BSS].address,
        layout->image_size,
        layout->image_size,
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(computed) == BUSTER_ARRAY_LENGTH(executable_linker_names), "a value per name");
    memcpy(values, computed, sizeof(computed));
}

// A symbol's reference (see the orientation comment), or the error it gets. A dynamic PIE's imports, and the symbols
// that its stubs stand for, get theirs from executable_resolve and executable_place, and so do a static executable's
// IFUNCs with stubs.
static ExecutableError executable_reference(const Link* link, const ExecutableLayout* layout, SymbolId symbol, uint32_t* reference)
{
    const AtomId atom = link->symbols.atom[symbol];
    const uint8_t source = link->symbols.source[symbol];
    const bool weak = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const ExecutableSectionLayout* tdata = &layout->sections[EXECUTABLE_SECTION_TDATA];
    ExecutableError result = EXECUTABLE_ERROR_NONE;
    if (atom != 0 && link_class_is_tls(link->atoms.class[atom]))
    {
        *reference = (uint32_t)(link->atoms.address[atom] + link->symbols.value[symbol] - tdata->address - layout->tls_block);
    }
    else if (atom != 0)
    {
        *reference = (uint32_t)(link->atoms.address[atom] + link->symbols.value[symbol]);
    }
    else if (source == BUSTER_LINK_SOURCE_ABSOLUTE)
    {
        // Its value, as an image offset, which the patch takes as an address again; one farther than int32 from the
        // base can't be one.
        const int64_t offset = (int64_t)link->symbols.value[symbol] - (int64_t)layout->base;
        *reference = (uint32_t)offset;
        result = offset == (int32_t)offset ? EXECUTABLE_ERROR_NONE : EXECUTABLE_ERROR_UNSUPPORTED;
    }
    else if (source == BUSTER_LINK_SOURCE_LINKER)
    {
        uint64_t values[BUSTER_ARRAY_LENGTH(executable_linker_names)];
        executable_linker_values(layout, values);
        uint32_t found = BUSTER_ARRAY_LENGTH(executable_linker_names);
        for (uint32_t i = 0; found == BUSTER_ARRAY_LENGTH(executable_linker_names) && i < BUSTER_ARRAY_LENGTH(executable_linker_names); i += 1)
        {
            found = executable_named(link, symbol, executable_linker_names[i]) ? i : found;
        }
        *reference = found < BUSTER_ARRAY_LENGTH(executable_linker_names) ? (uint32_t)values[found] : 0;
        result = found < BUSTER_ARRAY_LENGTH(executable_linker_names) ? EXECUTABLE_ERROR_NONE : EXECUTABLE_ERROR_UNSUPPORTED;
    }
    else if (source == BUSTER_LINK_SOURCE_NONE && weak)
    {
        // Address 0, as an image offset; for a thread-local one, an offset of 0 from the thread pointer, in TPOFF32
        // fields and GOT slots alike, as GNU ld gives it.
        *reference = executable_is_tls(link, symbol) ? 0 : (uint32_t)(0 - layout->base);
    }
    else if (source == BUSTER_LINK_SOURCE_NONE)
    {
        result = EXECUTABLE_ERROR_UNDEFINED;
    }
    else
    {
        result = EXECUTABLE_ERROR_UNSUPPORTED;
    }
    return result;
}

// A PIE's relocation class of a symbol: what its address needs when the program loads. Nothing for a TLS symbol (its
// offset from the thread pointer is the executable's to know), a weak reference to nothing that isn't dynamic (address
// 0) or an absolute symbol (its value wherever the image loads, as GNU ld and lld leave it); RELATIVE for a symbol of
// the image's, to which the load address adds; SYMBOLIC, in a dynamic PIE (`imports`), for an import, and for a weak
// reference to nothing with default visibility and a name, which a library may define, as GNU ld and lld make it: the
// loader looks them up in .dynsym. The SYMBOLIC ones are .dynsym's undefined entries. A static PIE has none: no library
// can define a weak reference to nothing, which is address 0, as GNU ld and lld leave it there; and a symbol of a shared
// library, which binding against the static runtime never makes, has no class (executable_reference refuses it).
#define BUSTER_EXECUTABLE_RELOCATION_NONE 0
#define BUSTER_EXECUTABLE_RELOCATION_RELATIVE 1
#define BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC 2

static uint8_t executable_relocation_class(const Link* link, SymbolId symbol, bool imports)
{
    const LinkSymbols* symbols = &link->symbols;
    const uint8_t flags = symbols->flags[symbol];
    const uint8_t source = symbols->source[symbol];
    const bool tls = executable_is_tls(link, symbol);
    const bool nowhere = source == BUSTER_LINK_SOURCE_NONE && symbols->atom[symbol] == 0;
    const bool weak = (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const bool visible = (flags & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_DEFAULT;
    const bool shared = source == BUSTER_LINK_SOURCE_RUNTIME_SHARED;
    uint8_t result = BUSTER_EXECUTABLE_RELOCATION_RELATIVE;
    if (imports && (shared || (nowhere && weak && visible && !tls && symbols->name[symbol] != 0)))
    {
        result = BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC;
    }
    else if (tls || nowhere || shared || source == BUSTER_LINK_SOURCE_ABSOLUTE)
    {
        result = BUSTER_EXECUTABLE_RELOCATION_NONE;
    }
    return result;
}

// Whether a dynamic PIE refuses a fixup of `kind` to an absolute symbol: one measured from its field (CALL, PC32 and
// PC64), which no load address makes right, since the loader moves the field and not the symbol. GNU ld and lld refuse
// them too.
static bool executable_refuses_absolute(uint8_t kind)
{
    return kind == BUSTER_LINK_FIXUP_CALL || kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_PC64;
}

// Whether a PIE's symbol is at address 0 wherever the image loads: a weak reference to nothing that isn't dynamic or
// thread-local (relocation class NONE), as hidden ones are, and every one in a static PIE. Its reference is address 0's
// image offset, right for an absolute field, which no relocation then changes, but not for a field measured from where
// it is: the loader moves the field, and not address 0.
static bool executable_is_absolute_zero(const Link* link, SymbolId symbol, bool imports)
{
    const bool weak = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    return weak && link->symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE && link->symbols.atom[symbol] == 0 && !executable_is_tls(link, symbol) &&
           executable_relocation_class(link, symbol, imports) == BUSTER_EXECUTABLE_RELOCATION_NONE;
}

// Whether the loader writes a class's atoms, so that a dynamic relocation can patch them: those of the RELRO and RW
// segments with bytes. A table of 64 bytes, so that 64 atoms' classes look it up at once.
static const uint8_t executable_written_classes[64] __attribute__((aligned(64))) = {
    [BUSTER_LINK_CLASS_TDATA] = 1,
    [BUSTER_LINK_CLASS_INIT_ARRAY] = 1,
    [BUSTER_LINK_CLASS_FINI_ARRAY] = 1,
    [BUSTER_LINK_CLASS_DATA_RELRO] = 1,
    [BUSTER_LINK_CLASS_DATA] = 1,
};

static bool executable_class_is_written(uint8_t class)
{
    return class < BUSTER_LINK_CLASS_COUNT && executable_written_classes[class] != 0;
}

// Whether a fixup's kind reads its target's GOT slot.
static bool executable_kind_reads_got(uint8_t kind)
{
    return kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL || kind == BUSTER_LINK_FIXUP_GOTTPOFF;
}

// A dynamic PIE's filter of the symbols that the image doesn't define (sources NONE, RUNTIME_SHARED and ABSOLUTE: weak
// references to nothing, imports and absolute symbols): a bit per hash of each, BUSTER_EXECUTABLE_FILTER_WORDS 32-bit
// words in two registers, so that 16 fixups' targets are tested at once without a load ("permutes as table lookups" in
// docs/object-writer-and-linker.md). A clear bit means that the image defines the target, so that no fixup to it needs
// a stub or is refused, and an ABS64 to it is RELATIVE. A set bit can be another symbol's (about the share of the 1,024 bits that are
// set: a few hundredths with tens of such symbols), which the exact check tells apart. A static executable's filter of
// its IFUNCs (with the C runtime) has a second hash, its bits in two registers of their own, and takes a target for an
// IFUNC only if both its bits are set: its symbols are the joined members' IFUNCs, more than a PIE's outside the image,
// and one hash took a 46th of a static link's targets for them on the 9700X (39,664 of the 1.81 million that don't read
// a GOT slot, in a link of 37 IFUNCs that 258 of them reach), both hashes 1,613.
#define BUSTER_EXECUTABLE_FILTER_WORDS 32
#define BUSTER_EXECUTABLE_FILTER_MULTIPLIER 0x9e3779b1u
#define BUSTER_EXECUTABLE_FILTER_SECOND_MULTIPLIER 0x85ebca6bu

typedef struct ExecutableFilter ExecutableFilter;
struct ExecutableFilter
{
    __m512i low;
    __m512i high;
    // The second hash's, zeros but for a static executable's IFUNCs.
    __m512i second_low;
    __m512i second_high;
};

static uint32_t executable_filter_bit(uint32_t symbol)
{
    return (symbol * BUSTER_EXECUTABLE_FILTER_MULTIPLIER) >> 22;
}

static uint32_t executable_filter_second_bit(uint32_t symbol)
{
    return (symbol * BUSTER_EXECUTABLE_FILTER_SECOND_MULTIPLIER) >> 22;
}

// Of the targets in `lanes`, those whose bits are set.
static __mmask16 executable_filter_test(const ExecutableFilter* filter, __m512i targets, __mmask16 lanes)
{
    const __m512i bits = _mm512_srli_epi32(_mm512_mullo_epi32(targets, _mm512_set1_epi32((int)BUSTER_EXECUTABLE_FILTER_MULTIPLIER)), 22);
    const __m512i words = _mm512_permutex2var_epi32(filter->low, _mm512_srli_epi32(bits, 5), filter->high);
    const __m512i shifted = _mm512_srlv_epi32(words, _mm512_and_si512(bits, _mm512_set1_epi32(31)));
    return _mm512_mask_test_epi32_mask(lanes, shifted, _mm512_set1_epi32(1));
}

// Of the targets in `lanes`, those whose bits are set by both hashes: a static executable's IFUNC filter's test.
static __mmask16 executable_filter_test_both(const ExecutableFilter* filter, __m512i targets, __mmask16 lanes)
{
    const __mmask16 first = executable_filter_test(filter, targets, lanes);
    const __m512i bits = _mm512_srli_epi32(_mm512_mullo_epi32(targets, _mm512_set1_epi32((int)BUSTER_EXECUTABLE_FILTER_SECOND_MULTIPLIER)), 22);
    const __m512i words = _mm512_permutex2var_epi32(filter->second_low, _mm512_srli_epi32(bits, 5), filter->second_high);
    const __m512i shifted = _mm512_srlv_epi32(words, _mm512_and_si512(bits, _mm512_set1_epi32(31)));
    return _mm512_mask_test_epi32_mask(first, shifted, _mm512_set1_epi32(1));
}

// The lanes' parts of a filter, together: the BUSTER_EXECUTABLE_FILTER_WORDS words from word `first` of each lane's
// part, and with `both`, the second hash's after them.
static ExecutableFilter executable_filter(const ExecutableShared* shared, uint32_t lanes, uint32_t first, bool both)
{
    ExecutableFilter result = {
        .low = _mm512_setzero_si512(),
        .high = _mm512_setzero_si512(),
        .second_low = _mm512_setzero_si512(),
        .second_high = _mm512_setzero_si512(),
    };
    for (uint32_t lane = 0; shared->filters != 0 && lane < lanes; lane += 1)
    {
        const uint32_t* part = shared->filters + shared->filter_words * lane + first;
        result.low = _mm512_or_si512(result.low, _mm512_load_si512(part));
        result.high = _mm512_or_si512(result.high, _mm512_load_si512(part + 16));
        if (both)
        {
            result.second_low = _mm512_or_si512(result.second_low, _mm512_load_si512(part + BUSTER_EXECUTABLE_FILTER_WORDS));
            result.second_high = _mm512_or_si512(result.second_high, _mm512_load_si512(part + BUSTER_EXECUTABLE_FILTER_WORDS + 16));
        }
    }
    return result;
}

static bool executable_bit(const uint64_t* bits, uint64_t index)
{
    return (bits[index / 64] >> (index % 64) & 1) != 0;
}

// What a dynamic symbol, found in this lane's range, adds to its counts: its .dynsym entry, its name's bytes and the
// version an import needs.
static void executable_count_dynamic_symbol(const Link* link, ExecutableShared* shared, uint32_t lane, SymbolId symbol)
{
    const LinkSymbols* symbols = &link->symbols;
    ExecutableDynamicCounts* counts = &shared->dynamic_counts[lane];
    const uint16_t version = symbols->source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED ? symbols->version[symbol] : 0;
    counts->symbols += 1;
    counts->name_bytes += (uint64_t)link->names.length[symbols->name[symbol]] + 1;
    if (version < shared->version_limit)
    {
        shared->versions_used[lane * shared->version_words + version / 64] |= 1ull << (version % 64);
    }
}

// A PIE's symbols, 64 at a time over this lane's range of words of the bitsets: the SYMBOLIC and RELATIVE ones
// (executable_relocation_class's), each set of bits a word of its bitset; in a static PIE, after
// executable_mark_ifuncs, the relaxable ones (the RELATIVE ones but the IFUNCs); this lane's part of the filter of the
// symbols the image doesn't define; and what the dynamic ones add to its counts, one at a time. The classes are lane
// masks over the source and flags columns, which link_check makes enough: a symbol of source NONE or ABSOLUTE has no
// atom, and a TLS symbol is one by its type, its atom's class's. Only the words with a weak reference to nothing of
// default visibility read the names (whether it has one), and only in a dynamic PIE. With `exact`, its twin:
// executable_relocation_class of one symbol at a time.
static void executable_mark_symbols(const Link* link, ExecutableShared* shared, uint32_t lane)
{
    const LinkSymbols* symbols = &link->symbols;
    uint32_t* filter = shared->filters + shared->filter_words * lane;
    memset(filter, 0, BUSTER_EXECUTABLE_FILTER_WORDS * sizeof(uint32_t));
    memset(shared->versions_used + lane * shared->version_words, 0, shared->version_words * sizeof(uint64_t));
    const Range64 words = lane_range(shared->words_per_lane);
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        const uint64_t first = 64 * word;
        const uint64_t count = symbols->count > first ? symbols->count - first : 0;
        // Symbol 0 is none.
        const __mmask64 valid = (count >= 64 ? ~0ull : (1ull << count) - 1) & (word == 0 ? ~1ull : ~0ull);
        uint64_t dynamic = 0;
        uint64_t relative = 0;
        uint64_t outside = 0;
        if (shared->exact)
        {
            for (uint64_t bit = 0; bit < 64; bit += 1)
            {
                const SymbolId symbol = (SymbolId)(first + bit);
                const bool symbol_valid = (valid >> bit & 1) != 0;
                const uint8_t class = symbol_valid ? executable_relocation_class(link, symbol, shared->imports) : BUSTER_EXECUTABLE_RELOCATION_NONE;
                const uint8_t source = symbol_valid ? symbols->source[symbol] : BUSTER_LINK_SOURCE_PROGRAM;
                const bool is_outside = source == BUSTER_LINK_SOURCE_NONE || source == BUSTER_LINK_SOURCE_RUNTIME_SHARED || source == BUSTER_LINK_SOURCE_ABSOLUTE;
                dynamic |= (uint64_t)(class == BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC) << bit;
                relative |= (uint64_t)(class == BUSTER_EXECUTABLE_RELOCATION_RELATIVE) << bit;
                outside |= (uint64_t)is_outside << bit;
            }
        }
        else
        {
            const __m512i source = _mm512_maskz_loadu_epi8(valid, symbols->source + first);
            const __m512i flags = _mm512_maskz_loadu_epi8(valid, symbols->flags + first);
            const uint64_t import = _mm512_mask_cmpeq_epi8_mask(valid, source, _mm512_set1_epi8(BUSTER_LINK_SOURCE_RUNTIME_SHARED));
            const uint64_t nowhere = _mm512_mask_cmpeq_epi8_mask(valid, source, _mm512_set1_epi8(BUSTER_LINK_SOURCE_NONE));
            const uint64_t absolute = _mm512_mask_cmpeq_epi8_mask(valid, source, _mm512_set1_epi8(BUSTER_LINK_SOURCE_ABSOLUTE));
            const uint64_t weak = _mm512_cmpeq_epi8_mask(_mm512_and_si512(flags, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_BINDING)), _mm512_set1_epi8(BUSTER_LINK_SYMBOL_WEAK));
            const uint64_t visible = _mm512_cmpeq_epi8_mask(_mm512_and_si512(flags, _mm512_set1_epi8((char)BUSTER_LINK_SYMBOL_VISIBILITY)),
                                                            _mm512_set1_epi8(BUSTER_LINK_SYMBOL_DEFAULT));
            const uint64_t tls = _mm512_cmpeq_epi8_mask(_mm512_and_si512(flags, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_TYPE)), _mm512_set1_epi8(BUSTER_LINK_SYMBOL_TLS));
            const uint64_t candidates = shared->imports ? nowhere & weak & visible & ~tls : 0;
            uint64_t named = 0;
            for (uint32_t part = 0; candidates != 0 && part < 4; part += 1)
            {
                const __mmask16 lanes = (__mmask16)(candidates >> (16 * part));
                const __m512i names = _mm512_maskz_loadu_epi32(lanes, symbols->name + first + 16 * part);
                named |= (uint64_t)_mm512_mask_cmpneq_epi32_mask(lanes, names, _mm512_setzero_si512()) << (16 * part);
            }
            dynamic = shared->imports ? import | (candidates & named) : 0;
            relative = valid & ~import & ~tls & ~nowhere & ~absolute;
            outside = import | nowhere | absolute;
        }
        shared->dynamic_bits[word] = dynamic;
        shared->relative_bits[word] = relative;
        if (shared->relax)
        {
            shared->relaxable_bits[word] = relative & ~shared->ifunc_bits[word];
        }
        for (uint64_t left = dynamic; left != 0; left &= left - 1)
        {
            executable_count_dynamic_symbol(link, shared, lane, (SymbolId)(first + (uint64_t)__builtin_ctzll(left)));
        }
        // A static PIE's go into the IFUNCs' filter of two hashes too (executable_mark_ifuncs wrote this lane's part of
        // it first), which its count tests the targets against for both (executable_count).
        uint32_t* both = shared->relax ? shared->filters + shared->filter_words * lane + shared->ifunc_filter : 0;
        for (uint64_t left = outside; left != 0; left &= left - 1)
        {
            const uint32_t symbol = (uint32_t)(first + (uint64_t)__builtin_ctzll(left));
            const uint32_t bit = executable_filter_bit(symbol);
            const uint32_t second = executable_filter_second_bit(symbol);
            filter[bit / 32] |= 1u << (bit % 32);
            if (both != 0)
            {
                both[bit / 32] |= 1u << (bit % 32);
                both[BUSTER_EXECUTABLE_FILTER_WORDS + second / 32] |= 1u << (second % 32);
            }
        }
    }
}

// A PIE's fixup that doesn't read a GOT slot and isn't ABS64, whose target the filter (or with `exact`, its atom) says
// may be outside the image. A CALL to a dynamic symbol goes to the symbol's stub, which reads its GOT slot: both are
// marked in this lane's bitsets. Any other kind of fixup to a dynamic symbol can't be relocated (lld refuses it too: the
// code isn't position-independent), and nor can a PC32 or PC64 to address 0 (executable_is_absolute_zero) or a CALL,
// PC32 or PC64 to an absolute symbol (executable_refuses_absolute); they are only counted here, and
// executable_check_relocations reports them. A CALL to address 0 is patched, as GNU ld and lld patch it: whatever it
// reaches, the program can only call it after checking that the symbol has an address. Most of the filter's hits are
// the image's symbols, whose bit in the RELATIVE bitset ends the check before their columns are read.
static void executable_check_direct(const Link* link, ExecutableShared* shared, uint32_t lane, FixupId fixup, uint64_t* refused)
{
    const SymbolId target = link->fixups.target[fixup];
    if (!executable_bit(shared->relative_bits, target))
    {
        const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        const bool call = kind == BUSTER_LINK_FIXUP_CALL;
        const bool relative = kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_PC64;
        const bool dynamic = executable_bit(shared->dynamic_bits, target);
        const bool absolute = link->symbols.source[target] == BUSTER_LINK_SOURCE_ABSOLUTE;
        const uint64_t word = lane * shared->words_per_lane + target / 64;
        const uint64_t bit = dynamic && call ? 1ull << (target % 64) : 0;
        shared->got_marks[word] |= bit;
        shared->stub_marks[word] |= bit;
        *refused += (dynamic && !call) || (relative && !dynamic && executable_is_absolute_zero(link, target, shared->imports)) ||
                            (absolute && executable_refuses_absolute(kind))
                        ? 1
                        : 0;
    }
}

// Whether a static executable's symbol is an IFUNC with a resolver, the code at it: of the type and defined in an atom.
// A weak IFUNC defined nowhere has none, and is address 0, as any weak symbol defined nowhere is (cc -static's links
// leave its GOT slot 0).
static bool executable_is_ifunc(const Link* link, SymbolId symbol)
{
    return (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_IFUNC && link->symbols.atom[symbol] != 0;
}

// The static runtime's IFUNCs with resolvers (a static executable's with the C runtime, or a static PIE's;
// executable_is_ifunc), 64 symbols at a time over this lane's range of words: their bitset's words, a compare of the
// type in the flags column and, for the words that have IFUNCs, of the atoms of those 16 at a time with 0; and this
// lane's part of the IFUNCs' filter, which the count tests the fixups' targets against (a bit per hash of each, by both
// hashes, as a PIE's filter has the symbols outside the image by one). With `exact`, its twin: one symbol at a time.
static void executable_mark_ifuncs(const Link* link, ExecutableShared* shared, uint32_t lane)
{
    const LinkSymbols* symbols = &link->symbols;
    uint32_t* filter = shared->filters + shared->filter_words * lane + shared->ifunc_filter;
    memset(filter, 0, 2 * BUSTER_EXECUTABLE_FILTER_WORDS * sizeof(uint32_t));
    const Range64 words = lane_range(shared->words_per_lane);
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        const uint64_t first = 64 * word;
        const uint64_t count = symbols->count > first ? symbols->count - first : 0;
        // Symbol 0 is none.
        const __mmask64 valid = (count >= 64 ? ~0ull : (1ull << count) - 1) & (word == 0 ? ~1ull : ~0ull);
        uint64_t ifuncs = 0;
        if (shared->exact)
        {
            for (uint64_t bit = 0; bit < 64; bit += 1)
            {
                const bool ifunc = (valid >> bit & 1) != 0 && executable_is_ifunc(link, (SymbolId)(first + bit));
                ifuncs |= (uint64_t)ifunc << bit;
            }
        }
        else
        {
            const __m512i types = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, symbols->flags + first), _mm512_set1_epi8(BUSTER_LINK_SYMBOL_TYPE));
            const uint64_t typed = _mm512_mask_cmpeq_epi8_mask(valid, types, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_IFUNC));
            for (uint32_t part = 0; typed != 0 && part < 4; part += 1)
            {
                const __mmask16 lanes = (__mmask16)(typed >> (16 * part));
                const __m512i atoms = _mm512_maskz_loadu_epi32(lanes, symbols->atom + first + 16 * part);
                ifuncs |= (uint64_t)_mm512_mask_cmpneq_epi32_mask(lanes, atoms, _mm512_setzero_si512()) << (16 * part);
            }
        }
        shared->ifunc_bits[word] = ifuncs;
        for (uint64_t left = ifuncs; left != 0; left &= left - 1)
        {
            const uint32_t symbol = (uint32_t)(first + (uint64_t)__builtin_ctzll(left));
            const uint32_t bit = executable_filter_bit(symbol);
            const uint32_t second = executable_filter_second_bit(symbol);
            filter[bit / 32] |= 1u << (bit % 32);
            filter[BUSTER_EXECUTABLE_FILTER_WORDS + second / 32] |= 1u << (second % 32);
        }
    }
}

// A static executable's fixup that doesn't read a GOT slot, whose target the filter (or with `exact`, every one) may be
// an IFUNC: an IFUNC that such a fixup reaches gets a stub, which stands for its address everywhere (as lld's canonical
// PLT entry does, so that &f is the same however it is taken) and jumps through a slot of its own after the GOT kinds',
// which an IRELATIVE relocation fills at startup. Its bit is marked in this lane's bitset of the stubs, for an IFUNC by
// its bit (with `exact`, executable_is_ifunc).
static void executable_mark_ifunc_target(const Link* link, ExecutableShared* shared, uint32_t lane, SymbolId target)
{
    const bool ifunc = shared->exact ? executable_is_ifunc(link, target) : executable_bit(shared->ifunc_bits, target);
    shared->stub_marks[lane * shared->words_per_lane + target / 64] |= ifunc ? 1ull << (target % 64) : 0;
}

// Of a word of the GOT's marks, the IFUNCs' slots that IRELATIVE relocations fill: those of IFUNCs without a stub (one
// with a stub has its stub's address there, as every reference to it has). From the bitsets, or with `exact`,
// executable_is_ifunc of each symbol.
static uint64_t executable_ifunc_slots(const Link* link, const ExecutableShared* shared, uint64_t word, uint64_t marks)
{
    uint64_t result = 0;
    if (shared->exact)
    {
        for (uint64_t left = marks; left != 0; left &= left - 1)
        {
            const uint64_t bit = (uint64_t)__builtin_ctzll(left);
            result |= executable_is_ifunc(link, (SymbolId)(word * 64 + bit)) ? 1ull << bit : 0;
        }
    }
    else
    {
        result = marks & shared->ifunc_bits[word];
    }
    return result & ~shared->stub_marks[word];
}

// A lane's share of the .eh_frame atoms (executable_count_frames): in id order, each atom and its FDEs' count, with
// BUSTER_EXECUTABLE_FRAMES_WALKED for one that the exact walk read, whose table entries it then writes too.
#define BUSTER_EXECUTABLE_FRAMES_WALKED 0x80000000u

typedef struct ExecutableFrames ExecutableFrames;
struct ExecutableFrames
{
    AtomId* atoms;
    uint32_t* fdes;
    uint32_t count;
};

// The .eh_frame atoms 64 at a time from `group`: a bit each, by a compare of their classes.
static uint64_t executable_frame_atoms(const LinkAtoms* atoms, AtomId group)
{
    const uint64_t left = atoms->count - group;
    const __mmask64 valid = left >= 64 ? ~0ull : (1ull << left) - 1;
    return _mm512_mask_cmpeq_epi8_mask(valid, _mm512_maskz_loadu_epi8(valid, atoms->class + group), _mm512_set1_epi8(BUSTER_LINK_CLASS_EH_FRAME));
}

// This lane's share of the .eh_frame atoms, by their cost (their fixups, which their FDEs' pc_begins are, plus one): the
// atoms whose cost's midpoint falls in the lane's share of the total, in id order, as lane_range_by_cost cuts, so that
// the lanes' shares follow one another and the atoms of a cluster (a generated program's, after its code) don't fall to
// one lane. Every lane finds the atoms twice, for the total, then for its share, whose FDEs (executable_eh_frame) go to
// its counts, and whose atoms to `frames`, in `arena`, for executable_eh_frame_table. An atom the header can't index
// fails the link.
static void executable_count_frames(const Link* link, ExecutableShared* shared, uint32_t lane, uint32_t lanes, ExecutableFrames* frames, Arena* arena, ExecutableLaneError* error)
{
    const LinkAtoms* atoms = &link->atoms;
    uint64_t total = 0;
    uint64_t atom_count = 0;
    for (AtomId group = 1; group < atoms->count; group += 64)
    {
        const uint64_t found_atoms = executable_frame_atoms(atoms, group);
        for (uint64_t found = found_atoms; found != 0; found &= found - 1)
        {
            total += (uint64_t)atoms->fixup_count[group + (AtomId)__builtin_ctzll(found)] + 1;
        }
        atom_count += (uint64_t)__builtin_popcountll(found_atoms);
    }
    *frames = (ExecutableFrames){
        .atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, atom_count),
        .fdes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, atom_count),
    };
    const bool room = frames->atoms != 0 && frames->fdes != 0;
    if (!room)
    {
        executable_lane_fail(error, EXECUTABLE_ERROR_MEMORY, 0, 0, 0);
    }
    uint64_t before = 0;
    uint64_t fdes = 0;
    for (AtomId group = 1; room && group < atoms->count; group += 64)
    {
        for (uint64_t found = executable_frame_atoms(atoms, group); found != 0; found &= found - 1)
        {
            const AtomId atom = group + (AtomId)__builtin_ctzll(found);
            const uint64_t cost = (uint64_t)atoms->fixup_count[atom] + 1;
            // The lane whose share of twice the total holds twice the midpoint.
            const uint64_t owner = (2 * before + cost) * lanes / (2 * total);
            before += cost;
            if (owner == lane)
            {
                bool walked = false;
                const uint32_t count = executable_eh_frame(link, atom, shared->exact, &walked);
                if (count == UINT32_MAX)
                {
                    executable_lane_fail(error, EXECUTABLE_ERROR_UNWIND, atom, 0, 0);
                }
                frames->atoms[frames->count] = atom;
                frames->fdes[frames->count] = (count != UINT32_MAX ? count : 0) | (walked ? BUSTER_EXECUTABLE_FRAMES_WALKED : 0);
                frames->count += 1;
                fdes += count != UINT32_MAX ? count : 0;
            }
        }
    }
    shared->counts[lane].fdes = fdes;
}

// Counts this lane's atoms per bucket (and each ordered class's largest alignment), and marks in its bitset the targets
// of the GOT kinds among its fixups, whose largest addend magnitude it records: in a static PIE, but those of the loads
// and calls that may relax (executable_relax_got), which are candidates in a row of their own (relax_marks). For a PIE,
// after executable_mark_symbols, it also counts its ABS64 fixups and its ABS32 and ABS32S ones, and tests the targets
// of the others against `filter`, 16 at a time, sending those it passes to executable_check_direct (with `exact`, those
// whose atom is 0, one at a time). With the static runtime, after executable_mark_ifuncs, it tests the targets of the
// fixups that don't read a slot against `ifunc_filter` for the IFUNCs' stubs, and counts the TLS accesses; a static PIE
// tests both kinds of target against `ifunc_filter` alone, which has its symbols outside the image too. Each 16 targets
// are loaded once for every test. Failures go to `error`.
static void executable_count(Link* link, ExecutableShared* shared, uint32_t lane, bool dynamic, const ExecutableFilter* filter, const ExecutableFilter* ifunc_filter,
                             ExecutableLaneError* error)
{
    const LinkAtoms* atoms = &link->atoms;
    ExecutableCounts* counts = &shared->counts[lane];
    memset(counts, 0, sizeof(*counts));
    // The columns in locals, which the stores to the counts can't alias. A run of atoms of one bucket, as consecutive
    // atoms mostly are, is summed in registers: in the counts, each atom's add would wait for the last one's store. A
    // block of 16 that executable_atom_block takes adds its padded sizes and pieces at once; another (or with `exact`,
    // every one) goes one atom at a time.
    const uint8_t* class_of = atoms->class;
    const uint8_t* alignment_of = atoms->alignment_log2;
    const uint64_t* size_of = atoms->size;
    uint32_t run = 0;
    uint64_t run_bytes = 0;
    uint64_t run_pieces = 0;
    const Range64 atom_range = lane_range(atoms->count - 1);
    const AtomId atom_end = (AtomId)atom_range.end + 1;
    // The range's prioritized atoms, which the atom-at-a-time loop meets one after another: every init and fini array
    // atom goes through it, since no block takes their classes.
    const LinkPriorities* priorities = &link->priorities;
    uint32_t priority = executable_first_priority(priorities, (AtomId)atom_range.begin + 1);
    for (AtomId block = (AtomId)atom_range.begin + 1; block < atom_end; block += 16)
    {
        const AtomId block_end = atom_end - block > 16 ? block + 16 : atom_end;
        const ExecutableAtomBlock taken = shared->exact ? (ExecutableAtomBlock){ .taken = false } : executable_atom_block(atoms, block, executable_block_lanes(block, atom_end));
        if (taken.taken)
        {
            if (taken.bucket != run)
            {
                counts->bytes[run] += run_bytes;
                counts->pieces[run] += run_pieces;
                run = taken.bucket;
                run_bytes = 0;
                run_pieces = 0;
            }
            run_bytes += (uint64_t)_mm512_reduce_add_epi64(_mm512_add_epi64(taken.padded[0], taken.padded[1]));
            run_pieces += (uint64_t)__builtin_popcount(taken.in_file);
        }
        for (AtomId atom = taken.taken ? block_end : block; atom < block_end; atom += 1)
        {
            const uint8_t class = class_of[atom];
            const uint64_t size = size_of[atom];
            const uint32_t log2 = executable_alignment_log2(class, alignment_of[atom], size, shared->crt);
            const bool prioritized = priority < priorities->count && priorities->atom[priority] == atom;
            priority += prioritized ? 1 : 0;
            // Bounded before its padding is added, which could wrap: up to 2^32 atoms of 2 GB each sum to at most 2^63.
            if (log2 == BUSTER_EXECUTABLE_ALIGNMENTS || size > BUSTER_EXECUTABLE_MAX_ATOM_BYTES)
            {
                executable_lane_fail(error, EXECUTABLE_ERROR_LAYOUT, atom, 0, 0);
            }
            else if (prioritized)
            {
                // Entries, which executable_alignment_log2 took: no padding.
                counts->prioritized_bytes[executable_array_index(class)] += size;
                counts->prioritized_pieces[executable_array_index(class)] += executable_in_file(class, size) ? 1 : 0;
            }
            else
            {
                const uint32_t bucket = executable_bucket(class, log2);
                if (bucket != run)
                {
                    counts->bytes[run] += run_bytes;
                    counts->pieces[run] += run_pieces;
                    run = bucket;
                    run_bytes = 0;
                    run_pieces = 0;
                }
                run_bytes += executable_align(size, 1ull << log2);
                run_pieces += executable_in_file(class, size) ? 1 : 0;
                if (executable_class_is_ordered(class, shared->crt) && alignment_of[atom] > counts->ordered_log2[class])
                {
                    counts->ordered_log2[class] = alignment_of[atom];
                }
            }
        }
    }
    counts->bytes[run] += run_bytes;
    counts->pieces[run] += run_pieces;

    const LinkFixups* fixups = &link->fixups;
    uint64_t* marks = shared->got_marks + lane * shared->words_per_lane;
    memset(marks, 0, shared->words_per_lane * sizeof(*marks));
    // A static PIE's candidates, whose targets get slots unless they relax; any other executable's are its marks.
    uint64_t* candidates = shared->relax ? shared->relax_marks + lane * shared->words_per_lane : marks;
    if (shared->relax)
    {
        memset(candidates, 0, shared->words_per_lane * sizeof(*candidates));
    }
    if (dynamic || shared->runtime)
    {
        memset(shared->stub_marks + lane * shared->words_per_lane, 0, shared->words_per_lane * sizeof(uint64_t));
    }
    // 64 fixups at a time: a compare of their kinds finds the GOT kinds' (CALL_GOT, LOAD_GOT and GOTPCREL are 2 to 4,
    // and GOTTPOFF), whose targets get their bits one at a time, and the addend column's magnitudes go into a maximum.
    // In a static PIE, a CALL_GOT or LOAD_GOT whose addend is -4 relaxes when its target is relaxable, and is only a
    // candidate for a slot: compares of the kinds' bytes (a wide addend's has BUSTER_LINK_FIXUP_WIDE) and of the addends
    // find them.
    // Masked loads stay within the columns.
    const Range64 fixup_range = lane_range(fixups->count - 1);
    const FixupId end = (FixupId)fixup_range.end + 1;
    __m512i largest = _mm512_setzero_si512();
    uint64_t abs64 = 0;
    uint64_t absolute32 = 0;
    uint64_t refused = 0;
    uint64_t tls_accesses = 0;
    uint64_t relaxed_calls = 0;
    for (FixupId group = (FixupId)fixup_range.begin + 1; group < end; group += 64)
    {
        const uint64_t count = end - group;
        const __mmask64 valid = count >= 64 ? ~0ull : (1ull << count) - 1;
        const __m512i bytes = _mm512_maskz_loadu_epi8(valid, fixups->kind + group);
        const __m512i kinds = _mm512_and_si512(bytes, _mm512_set1_epi8(BUSTER_LINK_FIXUP_KIND));
        const __mmask64 slot_reads = _mm512_cmple_epu8_mask(_mm512_sub_epi8(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_CALL_GOT)), _mm512_set1_epi8(2));
        const uint64_t through_got = valid & (slot_reads | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_GOTTPOFF)));
        uint64_t minus_four = 0;
        for (uint32_t part = 0; part < 4; part += 1)
        {
            const __mmask16 lanes = (__mmask16)(valid >> (16 * part));
            const __m512i addends = _mm512_maskz_loadu_epi32(lanes, fixups->addend + group + 16 * part);
            largest = _mm512_max_epu32(largest, _mm512_abs_epi32(addends));
            minus_four |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(lanes, addends, _mm512_set1_epi32(-4)) << (16 * part);
        }
        const uint64_t relaxing = shared->relax ? through_got & minus_four &
                                                      (_mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(BUSTER_LINK_FIXUP_CALL_GOT)) |
                                                       _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(BUSTER_LINK_FIXUP_LOAD_GOT)))
                                                : 0;
        // Their targets get their bits in this lane's row of the candidates, the others' in its marks, one at a time;
        // executable_reduce_got marks the candidates whose targets don't relax, a word of symbols at a time.
        for (uint64_t got = through_got; got != 0; got &= got - 1)
        {
            const uint64_t bit = (uint64_t)__builtin_ctzll(got);
            const SymbolId target = fixups->target[group + (FixupId)bit];
            uint64_t* row = (relaxing >> bit & 1) != 0 ? candidates : marks;
            row[target / 64] |= 1ull << (target % 64);
        }
        // A PIE's ABS64, ABS32 and ABS32S fixups counted, and its direct ones (neither through the GOT nor ABS64) to
        // be tested against the filter of the symbols outside the image; the static runtime's TLS accesses counted by a
        // compare of the kinds, and its direct ones (not through the GOT) to be tested against its IFUNCs' filter, and
        // every fixup's target against __tls_get_addr, whose fixups must be the accesses' calls. Each 16 targets are
        // loaded once for every test that takes them: a static PIE takes all three.
        uint64_t pie_direct = 0;
        uint64_t ifunc_direct = 0;
        if (dynamic)
        {
            const uint64_t abs64_mask = valid & _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_ABS64));
            const uint64_t absolute32_mask =
                valid & (_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_ABS32)) | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_ABS32S)));
            abs64 += (uint64_t)__builtin_popcountll(abs64_mask);
            absolute32 += (uint64_t)__builtin_popcountll(absolute32_mask);
            pie_direct = valid & ~(through_got | abs64_mask);
        }
        if (shared->runtime)
        {
            const uint64_t accesses = valid & (_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_TLSGD)) |
                                               _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_TLSLD)));
            tls_accesses += (uint64_t)__builtin_popcountll(accesses);
            ifunc_direct = valid & ~through_got;
        }
        // The filters' hits are told apart by executable_check_direct and executable_mark_ifunc_target (with `exact`,
        // every target whose atom is 0, and every IFUNC target, one at a time).
        const uint64_t tested = (shared->exact ? 0 : pie_direct | ifunc_direct) | (shared->relaxed != 0 ? valid : 0);
        for (uint32_t part = 0; tested != 0 && part < 4; part += 1)
        {
            const __mmask16 lanes = (__mmask16)(tested >> (16 * part));
            const __m512i targets = _mm512_maskz_loadu_epi32(lanes, fixups->target + group + 16 * part);
            relaxed_calls += shared->relaxed != 0 ? (uint64_t)__builtin_popcount(_mm512_mask_cmpeq_epi32_mask(lanes, targets, _mm512_set1_epi32((int)shared->relaxed))) : 0;
            const __mmask16 pie_lanes = shared->exact ? 0 : (__mmask16)(pie_direct >> (16 * part));
            const __mmask16 ifunc_lanes = shared->exact ? 0 : (__mmask16)(ifunc_direct >> (16 * part));
            // A static PIE's targets against the IFUNCs' filter alone, which has the symbols outside the image too
            // (executable_mark_symbols): one test of two hashes for both checks. With the filter of one hash, a 33rd of
            // the targets hit the bits of its 55 symbols outside the image (49,000 of 1.6 million for 180,000 workers on
            // the 9700X), and the count took 2.0 ms where it takes 1.56.
            const uint32_t shared_hits = shared->relax && (pie_lanes | ifunc_lanes) != 0 ? executable_filter_test_both(ifunc_filter, targets, pie_lanes | ifunc_lanes) : 0;
            const uint32_t pie_hits = shared->relax ? shared_hits & pie_lanes : pie_lanes != 0 ? executable_filter_test(filter, targets, pie_lanes) : 0;
            for (uint32_t hits = pie_hits; hits != 0; hits &= hits - 1)
            {
                executable_check_direct(link, shared, lane, group + 16 * part + (FixupId)__builtin_ctz(hits), &refused);
            }
            const uint32_t ifunc_hits = shared->relax ? shared_hits & ifunc_lanes : ifunc_lanes != 0 ? executable_filter_test_both(ifunc_filter, targets, ifunc_lanes) : 0;
            for (uint32_t hits = ifunc_hits; hits != 0; hits &= hits - 1)
            {
                executable_mark_ifunc_target(link, shared, lane, fixups->target[group + 16 * part + (FixupId)__builtin_ctz(hits)]);
            }
        }
        for (uint64_t left = shared->exact ? pie_direct : 0; left != 0; left &= left - 1)
        {
            const FixupId fixup = group + (FixupId)__builtin_ctzll(left);
            if (link->symbols.atom[fixups->target[fixup]] == 0)
            {
                executable_check_direct(link, shared, lane, fixup, &refused);
            }
        }
        for (uint64_t left = shared->exact ? ifunc_direct : 0; left != 0; left &= left - 1)
        {
            executable_mark_ifunc_target(link, shared, lane, fixups->target[group + (FixupId)__builtin_ctzll(left)]);
        }
    }
    // INT32_MIN's absolute value reads as 2^31, unsigned.
    shared->addend_magnitudes[lane] = _mm512_reduce_max_epu32(largest);
    if (shared->runtime)
    {
        // __tls_get_addr's calls are relaxed away: it gets no slot, whichever row its calls marked.
        marks[shared->relaxed / 64] &= ~(shared->relaxed != 0 ? 1ull << (shared->relaxed % 64) : 0);
        candidates[shared->relaxed / 64] &= ~(shared->relaxed != 0 ? 1ull << (shared->relaxed % 64) : 0);
        shared->dynamic_counts[lane].tls_accesses = tls_accesses;
        shared->dynamic_counts[lane].relaxed_calls = relaxed_calls;
    }
    if (dynamic)
    {
        ExecutableDynamicCounts* dynamic_counts = &shared->dynamic_counts[lane];
        dynamic_counts->abs64_fixups = abs64;
        dynamic_counts->absolute32 = absolute32;
        dynamic_counts->refused = refused;
    }
}

// Of a word of the GOT's marks, the symbols whose slots a PIE relocates by class: from the bitsets, or with `exact`
// executable_relocation_class of each. In a static PIE, the slots of the IFUNCs without stubs, which IRELATIVE
// relocations fill (executable_ifunc_slots), aren't RELATIVE: an IFUNC with a stub has the stub's address in its slot,
// which is.
static uint64_t executable_slots_of_class(const Link* link, const ExecutableShared* shared, uint64_t word, uint64_t marks, uint8_t class)
{
    uint64_t result = 0;
    if (shared->exact)
    {
        for (uint64_t left = marks; left != 0; left &= left - 1)
        {
            const uint64_t bit = (uint64_t)__builtin_ctzll(left);
            result |= executable_relocation_class(link, (SymbolId)(word * 64 + bit), shared->imports) == class ? 1ull << bit : 0;
        }
    }
    else
    {
        result = marks & (class == BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC ? shared->dynamic_bits[word] : shared->relative_bits[word]);
    }
    const uint64_t irelative = shared->runtime && class == BUSTER_EXECUTABLE_RELOCATION_RELATIVE ? executable_ifunc_slots(link, shared, word, marks) : 0;
    return result & ~irelative;
}

// Of a word of a static PIE's candidates for slots (ExecutableShared.relax_marks), those whose targets don't relax: from
// the bitset of the relaxable symbols, or with `exact` by executable_relocation_class and executable_is_ifunc of each.
static uint64_t executable_unrelaxed(const Link* link, const ExecutableShared* shared, uint64_t word, uint64_t candidates)
{
    uint64_t result = 0;
    if (shared->exact)
    {
        for (uint64_t left = candidates; left != 0; left &= left - 1)
        {
            const uint64_t bit = (uint64_t)__builtin_ctzll(left);
            const SymbolId symbol = (SymbolId)(word * 64 + bit);
            const bool relaxable = executable_relocation_class(link, symbol, shared->imports) == BUSTER_EXECUTABLE_RELOCATION_RELATIVE && !executable_is_ifunc(link, symbol);
            result |= relaxable ? 0 : 1ull << bit;
        }
    }
    else
    {
        result = candidates & ~shared->relaxable_bits[word];
    }
    return result;
}

// ORs every lane's bitset into lane 0's, over this lane's range of words, and counts the slots there, a static PIE's
// candidates whose targets don't relax among them (executable_unrelaxed); for a dynamic PIE, the same of the stubs'
// bitsets, and the GOT slots' relocations, RELATIVE and SYMBOLIC, the RELATIVE ones .relr.dyn's with RELR; for a static
// executable with the C runtime, the same of the stubs' bitsets, and the IFUNCs' slots, which IRELATIVE relocations
// fill.
static void executable_reduce_got(const Link* link, ExecutableShared* shared, uint32_t lane, uint32_t lanes)
{
    const Range64 words = lane_range(shared->words_per_lane);
    const bool dynamic = shared->dynamic_bits != 0;
    const bool stubbed = dynamic || shared->runtime;
    const bool relr_slots = dynamic && shared->relr;
    uint64_t count = 0;
    uint64_t stubs = 0;
    uint64_t irelatives = 0;
    uint64_t relative = 0;
    uint64_t symbolic = 0;
    uint64_t relr_relocations = 0;
    uint64_t relr_words = 0;
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        uint64_t marks = 0;
        uint64_t stub_marks = 0;
        uint64_t candidates = 0;
        for (uint32_t other = 0; other < lanes; other += 1)
        {
            marks |= shared->got_marks[other * shared->words_per_lane + word];
            stub_marks |= stubbed ? shared->stub_marks[other * shared->words_per_lane + word] : 0;
            candidates |= shared->relax ? shared->relax_marks[other * shared->words_per_lane + word] : 0;
        }
        marks |= candidates != 0 ? executable_unrelaxed(link, shared, word, candidates) : 0;
        shared->got_marks[word] = marks;
        count += (uint64_t)__builtin_popcountll(marks);
        if (stubbed)
        {
            shared->stub_marks[word] = stub_marks;
            stubs += (uint64_t)__builtin_popcountll(stub_marks);
        }
        irelatives += shared->runtime ? (uint64_t)__builtin_popcountll(executable_ifunc_slots(link, shared, word, marks)) : 0;
        const uint64_t word_relative = dynamic ? (uint64_t)__builtin_popcountll(executable_slots_of_class(link, shared, word, marks, BUSTER_EXECUTABLE_RELOCATION_RELATIVE)) : 0;
        symbolic += dynamic ? (uint64_t)__builtin_popcountll(executable_slots_of_class(link, shared, word, marks, BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC)) : 0;
        // With RELR, a word's RELATIVE slots are .relr.dyn's: the slots of a word's marked symbols are consecutive, at
        // most 64, so they take an address entry and at most one bitmap (executable_got_relr).
        relative += relr_slots ? 0 : word_relative;
        relr_relocations += relr_slots ? word_relative : 0;
        relr_words += relr_slots ? (word_relative >= 1 ? 1 : 0) + (word_relative >= 2 ? 1 : 0) : 0;
    }
    shared->got_counts[lane] = count;
    if (shared->runtime)
    {
        shared->dynamic_counts[lane].stubs = stubs;
        shared->dynamic_counts[lane].irelatives = irelatives;
    }
    if (dynamic)
    {
        shared->dynamic_counts[lane].stubs = stubs;
        shared->dynamic_counts[lane].got_relative = relative;
        shared->dynamic_counts[lane].got_symbolic = symbolic;
        shared->dynamic_counts[lane].got_relr_words = relr_words;
        shared->dynamic_counts[lane].relr_relocations += relr_relocations;
        shared->dynamic_counts[lane].relr_words += relr_words;
    }
}

// RELR (SHT_RELR): a list of 64-bit words, each an even address, whose word the loader relocates, or an odd bitmap
// whose bits 1 to 63 say which of the 63 words after the last address entry's (or the last bitmap's) it relocates
// too. The encoder takes the addresses in increasing order and writes them as lld does: an address entry, then a
// bitmap for each run of 63 words after it while each has a bit set; an address it can't put in the run (beyond an
// empty window, or not a multiple of 8 bytes from it) starts another. With `words` 0, it only counts.
#define BUSTER_EXECUTABLE_RELR_BITS 63
// An atom's RELR words with this bit: it is dense (its fixups are all ABS64 to the image, 8 bytes apart from an even
// offset in an atom aligned to 2 or more), and has no other relocations; with BUSTER_EXECUTABLE_RELR_RELA, it has
// .rela.dyn's. The other bits count its words: an atom's are at most its 8-byte fields, which its size bounds well below
// them.
#define BUSTER_EXECUTABLE_RELR_DENSE (1u << 31)
#define BUSTER_EXECUTABLE_RELR_RELA (1u << 30)
#define BUSTER_EXECUTABLE_RELR_WORDS (BUSTER_EXECUTABLE_RELR_RELA - 1)

typedef struct ExecutableRelr ExecutableRelr;
struct ExecutableRelr
{
    uint64_t* words;
    uint64_t count;
    uint64_t where;
    uint64_t bitmap;
    bool open;
};

static void executable_relr_emit(ExecutableRelr* relr, uint64_t word)
{
    if (relr->words != 0)
    {
        relr->words[relr->count] = word;
    }
    relr->count += 1;
}

static void executable_relr_add(ExecutableRelr* relr, uint64_t address)
{
    bool placed = false;
    while (!placed)
    {
        const uint64_t distance = address - relr->where;
        const bool aligned = relr->open && address >= relr->where && distance % 8 == 0;
        if (aligned && distance < 8 * BUSTER_EXECUTABLE_RELR_BITS)
        {
            relr->bitmap |= 1ull << (distance / 8);
            placed = true;
        }
        else if (aligned && relr->bitmap != 0)
        {
            // Past the window, which has bits: its bitmap, then the next window.
            executable_relr_emit(relr, relr->bitmap << 1 | 1);
            relr->bitmap = 0;
            relr->where += 8 * BUSTER_EXECUTABLE_RELR_BITS;
        }
        else
        {
            if (relr->open && relr->bitmap != 0)
            {
                executable_relr_emit(relr, relr->bitmap << 1 | 1);
            }
            executable_relr_emit(relr, address);
            relr->where = address + 8;
            relr->bitmap = 0;
            relr->open = true;
            placed = true;
        }
    }
}

// Ends a run: its last bitmap, if it has bits.
static void executable_relr_end(ExecutableRelr* relr)
{
    if (relr->open && relr->bitmap != 0)
    {
        executable_relr_emit(relr, relr->bitmap << 1 | 1);
    }
    relr->open = false;
    relr->bitmap = 0;
}

// A dense atom's words, from its first relocation's address and how many it has: an address entry, then the full
// bitmaps and the last, which is what executable_relr_add makes of them.
static uint64_t executable_relr_dense(uint64_t* words, uint64_t address, uint64_t count)
{
    const uint64_t bitmaps = (count - 1 + BUSTER_EXECUTABLE_RELR_BITS - 1) / BUSTER_EXECUTABLE_RELR_BITS;
    if (words != 0)
    {
        words[0] = address;
        for (uint64_t b = 0; b < bitmaps; b += 1)
        {
            const uint64_t left = count - 1 - b * BUSTER_EXECUTABLE_RELR_BITS;
            const uint64_t bits = left < BUSTER_EXECUTABLE_RELR_BITS ? left : BUSTER_EXECUTABLE_RELR_BITS;
            words[1 + b] = ((1ull << bits) - 1) << 1 | 1;
        }
    }
    return 1 + bitmaps;
}

// Whether a RELATIVE relocation at `offset` in an atom aligned to 2^`alignment_log2` can be RELR's: its address is
// even, which before the layout is known only of an atom aligned to 2 or more.
static bool executable_relr_eligible(uint8_t alignment_log2, uint32_t offset)
{
    return alignment_log2 >= 1 && offset % 2 == 0;
}

// The relocation class of an ABS64 fixup's target: RELATIVE for one the image defines (link_check refuses ABS64 to a
// TLS symbol), the exact class otherwise. `outside` is whether the image may not define it: a bit of the filter's, or
// with `exact` its atom's being 0 (the linker's symbols too, which the exact class makes RELATIVE).
static uint8_t executable_abs64_class(const Link* link, SymbolId target, bool outside, bool imports)
{
    return outside ? executable_relocation_class(link, target, imports) : BUSTER_EXECUTABLE_RELOCATION_RELATIVE;
}

// Whether a written atom is dense: every fixup ABS64 to a RELATIVE target, 8 bytes apart from an even first offset, in
// an atom aligned to 2 or more; 16 fixups at a time, the targets that the filter says may be outside the image looked
// up in the RELATIVE bitset (a few hundredths of them, at most).
static bool executable_atom_is_dense(const Link* link, const ExecutableShared* shared, const ExecutableFilter* filter, AtomId atom)
{
    const LinkFixups* fixups = &link->fixups;
    const FixupId begin = link->atoms.fixup_begin[atom];
    const uint32_t count = link->atoms.fixup_count[atom];
    const uint32_t first = count != 0 ? fixups->offset[begin] : 1;
    bool dense = count != 0 && executable_relr_eligible(link->atoms.alignment_log2[atom], first);
    const __m512i steps = _mm512_setr_epi32(0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120);
    for (uint32_t group = 0; dense && group < count; group += 16)
    {
        const __mmask16 lanes = count - group >= 16 ? 0xffff : (__mmask16)((1u << (count - group)) - 1);
        const __m128i kinds = _mm_and_si128(_mm_maskz_loadu_epi8(lanes, fixups->kind + begin + group), _mm_set1_epi8(BUSTER_LINK_FIXUP_KIND));
        const __m512i targets = _mm512_maskz_loadu_epi32(lanes, fixups->target + begin + group);
        const __m512i offsets = _mm512_maskz_loadu_epi32(lanes, fixups->offset + begin + group);
        const __m512i expected = _mm512_add_epi32(_mm512_set1_epi32((int)(first + 8 * group)), steps);
        const bool kinds_abs64 = _mm_mask_cmpneq_epi8_mask(lanes, kinds, _mm_set1_epi8(BUSTER_LINK_FIXUP_ABS64)) == 0;
        const bool spaced = _mm512_mask_cmpneq_epi32_mask(lanes, offsets, expected) == 0;
        bool relative = true;
        for (uint32_t hits = executable_filter_test(filter, targets, lanes); hits != 0; hits &= hits - 1)
        {
            relative = relative && executable_bit(shared->relative_bits, fixups->target[begin + group + (uint32_t)__builtin_ctz(hits)]);
        }
        dense = kinds_abs64 && spaced && relative;
    }
    return dense;
}

// A written atom's ABS64 fixups, one at a time: into `counts`, how many, and how many .rela.dyn and .relr.dyn
// relocate; the RELR words they take. The filter says which targets may be outside the image, or with `exact` their
// atoms.
static uint64_t executable_classify_atom(const Link* link, const ExecutableShared* shared, const ExecutableFilter* filter, AtomId atom, ExecutableDynamicCounts* counts)
{
    const LinkFixups* fixups = &link->fixups;
    const FixupId begin = link->atoms.fixup_begin[atom];
    const uint8_t alignment_log2 = link->atoms.alignment_log2[atom];
    ExecutableRelr relr = { 0 };
    for (FixupId fixup = begin; fixup < begin + link->atoms.fixup_count[atom]; fixup += 1)
    {
        const bool abs64 = (fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_ABS64;
        const SymbolId target = fixups->target[fixup];
        const __mmask16 hit = shared->exact ? 0 : executable_filter_test(filter, _mm512_set1_epi32((int)target), 1);
        const bool outside = shared->exact ? link->symbols.atom[target] == 0 : hit != 0;
        const uint8_t class = abs64 ? executable_abs64_class(link, target, outside, shared->imports) : BUSTER_EXECUTABLE_RELOCATION_NONE;
        const bool relative = class == BUSTER_EXECUTABLE_RELOCATION_RELATIVE;
        const bool packed = relative && shared->relr && executable_relr_eligible(alignment_log2, fixups->offset[fixup]);
        counts->abs64_written += abs64 ? 1 : 0;
        counts->abs64_symbolic += class == BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC ? 1 : 0;
        counts->abs64_relative += relative && !packed ? 1 : 0;
        counts->relr_relocations += packed ? 1 : 0;
        if (packed)
        {
            executable_relr_add(&relr, fixups->offset[fixup]);
        }
    }
    executable_relr_end(&relr);
    return relr.count;
}

// Of the 64 atoms from `first`, those in `valid` that the loader writes: their classes looked up in
// executable_written_classes at once, or with `exact` executable_class_is_written of each.
static uint64_t executable_written_atoms(const LinkAtoms* atoms, const ExecutableShared* shared, AtomId first, uint64_t valid)
{
    uint64_t result = 0;
    if (shared->exact)
    {
        for (uint64_t left = valid; left != 0; left &= left - 1)
        {
            const uint64_t bit = (uint64_t)__builtin_ctzll(left);
            result |= executable_class_is_written(atoms->class[first + (AtomId)bit]) ? 1ull << bit : 0;
        }
    }
    else
    {
        // Every class is below BUSTER_LINK_CLASS_COUNT, so its low 6 bits are its index in the table.
        const __m512i classes = _mm512_maskz_loadu_epi8(valid, atoms->class + first);
        const __m512i written = _mm512_permutexvar_epi8(classes, _mm512_load_si512(executable_written_classes));
        result = _mm512_mask_test_epi8_mask(valid, written, written);
    }
    return result;
}

// A dynamic PIE's ABS64 fixups in this lane's range of the atoms the loader writes (found 64 at a time, with
// executable_written_atoms): how many, how many .rela.dyn relocates (to a dynamic symbol, R_X86_64_64; to the image,
// RELATIVE, when .relr.dyn can't: without RELR, or at an address that may be odd), and each atom's RELR words for the
// rest: an atom's RELATIVE relocations at even addresses encoded from its first one's (executable_relr_add), so that
// their words don't depend on where the layout puts the atom, and the section's size is known before it. A dense atom
// (executable_atom_is_dense, almost every pointer table) takes its counts and words from its fixup count alone. With
// `exact`, its twin: every atom one fixup at a time, its targets' atoms loaded.
static void executable_classify(const Link* link, ExecutableShared* shared, const ExecutableFilter* filter, uint32_t lane)
{
    const LinkAtoms* atoms = &link->atoms;
    ExecutableDynamicCounts* counts = &shared->dynamic_counts[lane];
    const Range64 range = lane_range(atoms->count - 1);
    const AtomId end = (AtomId)range.end + 1;
    for (AtomId first = (AtomId)range.begin + 1; first < end; first += 64)
    {
        const uint64_t remaining = end - first;
        const uint64_t valid = remaining >= 64 ? ~0ull : (1ull << remaining) - 1;
        for (uint64_t left = executable_written_atoms(atoms, shared, first, valid); left != 0; left &= left - 1)
        {
            const AtomId atom = first + (AtomId)__builtin_ctzll(left);
            const bool dense = shared->relr && !shared->exact && executable_atom_is_dense(link, shared, filter, atom);
            const uint32_t count = atoms->fixup_count[atom];
            const uint64_t rela = counts->abs64_relative + counts->abs64_symbolic;
            uint64_t words = 0;
            if (dense)
            {
                words = executable_relr_dense(0, 0, count);
                counts->abs64_written += count;
                counts->relr_relocations += count;
            }
            else
            {
                words = executable_classify_atom(link, shared, filter, atom, counts);
            }
            counts->relr_words += words;
            if (shared->relr)
            {
                const bool has_rela = counts->abs64_relative + counts->abs64_symbolic != rela;
                shared->relr_words[atom] = (uint32_t)words | (dense ? BUSTER_EXECUTABLE_RELR_DENSE : 0) | (has_rela ? BUSTER_EXECUTABLE_RELR_RELA : 0);
            }
        }
    }
}

// Whether a PIE can't relocate a fixup of `kind` to `target` in an atom of class `atom_class`: an ABS64 that
// needs a relocation outside the atoms the loader writes (a text relocation), an ABS32 or ABS32S that needs one (no load
// address fits it), a fixup of another kind to a dynamic symbol, but for a GOT kind and a CALL, which goes to the
// symbol's stub, a PC32 or PC64 to address 0 (executable_is_absolute_zero), or a CALL, PC32 or PC64 to an absolute
// symbol (executable_refuses_absolute). Every other fixup to an absolute symbol is its value, which needs no relocation.
static bool executable_is_refused(const Link* link, uint8_t kind, SymbolId target, uint8_t atom_class, bool imports)
{
    const uint8_t class = executable_relocation_class(link, target, imports);
    const bool relocated = class != BUSTER_EXECUTABLE_RELOCATION_NONE;
    const bool absolute32 = kind == BUSTER_LINK_FIXUP_ABS32 || kind == BUSTER_LINK_FIXUP_ABS32S;
    const bool text = kind == BUSTER_LINK_FIXUP_ABS64 && !executable_class_is_written(atom_class);
    const bool direct = !executable_kind_reads_got(kind) && kind != BUSTER_LINK_FIXUP_ABS64 && kind != BUSTER_LINK_FIXUP_CALL;
    const bool relative = kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_PC64;
    const bool absolute = link->symbols.source[target] == BUSTER_LINK_SOURCE_ABSOLUTE;
    return (relocated && (text || absolute32)) || (direct && class == BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC) || (relative && executable_is_absolute_zero(link, target, imports)) ||
           (absolute && executable_refuses_absolute(kind));
}

// A PIE's fixups that it can't relocate (executable_is_refused), which the lanes' counts point to and lane 0
// finds, the exact path for what is rare: the smallest goes to `error`.
static void executable_check_relocations(const Link* link, const ExecutableShared* shared, uint32_t lanes, ExecutableLaneError* error)
{
    uint64_t abs64 = 0;
    uint64_t written = 0;
    uint64_t absolute32 = 0;
    uint64_t refused = 0;
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        abs64 += shared->dynamic_counts[lane].abs64_fixups;
        written += shared->dynamic_counts[lane].abs64_written;
        absolute32 += shared->dynamic_counts[lane].absolute32;
        refused += shared->dynamic_counts[lane].refused;
    }
    const LinkAtoms* atoms = &link->atoms;
    const LinkFixups* fixups = &link->fixups;
    FixupId failed = 0;
    for (AtomId atom = 1; (abs64 != written || absolute32 != 0 || refused != 0) && atom < atoms->count; atom += 1)
    {
        const FixupId begin = atoms->fixup_begin[atom];
        for (FixupId fixup = begin; fixup < begin + atoms->fixup_count[atom]; fixup += 1)
        {
            const uint8_t kind = fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const bool smaller = failed == 0 || fixup < failed;
            failed = smaller && executable_is_refused(link, kind, fixups->target[fixup], atoms->class[atom], shared->imports) ? fixup : failed;
        }
    }
    if (failed != 0)
    {
        executable_lane_fail(error, EXECUTABLE_ERROR_RELOCATION, 0, 0, failed);
    }
}

// .dynamic's entries, in lld's order: DT_NEEDED (a dynamic PIE's), DT_FLAGS_1 (a PIE), DT_DEBUG, the relocations'
// (.rela.dyn's, a static PIE's IRELATIVE ones among them, then .relr.dyn's), the symbols' and strings', DT_GNU_HASH,
// the init and fini arrays', DT_INIT and DT_FINI, the versions', and DT_NULL. With `entries` 0, only how many;
// otherwise their values from the layout, and DT_INIT's and DT_FINI's addresses.
#define BUSTER_EXECUTABLE_MAX_DYNAMIC_ENTRIES 28

static uint32_t executable_dynamic_entries(const ExecutableLayout* layout, const ExecutableDynamicTotals* totals, uint64_t init, uint64_t fini, Elf64_Dyn* entries)
{
    const ExecutableSectionLayout* sections = layout != 0 ? layout->sections : 0;
    const uint64_t base = layout != 0 ? layout->base : 0;
    const uint64_t relocations = totals->got_relative + totals->got_symbolic + totals->abs64_relative + totals->abs64_symbolic + totals->irelatives;
    const uint64_t relative = totals->got_relative + totals->abs64_relative;
    const uint64_t rela = sections != 0 ? base + sections[EXECUTABLE_SECTION_RELA_DYN].address : 0;
    const uint64_t relr = sections != 0 ? base + sections[EXECUTABLE_SECTION_RELR_DYN].address : 0;
    const uint64_t dynsym = sections != 0 ? base + sections[EXECUTABLE_SECTION_DYNSYM].address : 0;
    const uint64_t dynstr = sections != 0 ? base + sections[EXECUTABLE_SECTION_DYNSTR].address : 0;
    const uint64_t gnu_hash = sections != 0 ? base + sections[EXECUTABLE_SECTION_GNU_HASH].address : 0;
    const uint64_t versym = sections != 0 ? base + sections[EXECUTABLE_SECTION_VERSYM].address : 0;
    const uint64_t verneed = sections != 0 ? base + sections[EXECUTABLE_SECTION_VERNEED].address : 0;
    const ExecutableSectionLayout* init_array = sections != 0 ? &sections[EXECUTABLE_SECTION_INIT_ARRAY] : 0;
    const ExecutableSectionLayout* fini_array = sections != 0 ? &sections[EXECUTABLE_SECTION_FINI_ARRAY] : 0;
    const Elf64_Dyn all[BUSTER_EXECUTABLE_MAX_DYNAMIC_ENTRIES] = {
        { .d_tag = totals->needed[0] ? DT_NEEDED : DT_NULL, .d_un.d_val = totals->needed_names[0] },
        { .d_tag = totals->needed[1] ? DT_NEEDED : DT_NULL, .d_un.d_val = totals->needed_names[1] },
        { .d_tag = DT_FLAGS_1, .d_un.d_val = DF_1_PIE },
        { .d_tag = DT_DEBUG, .d_un.d_val = 0 },
        { .d_tag = relocations != 0 ? DT_RELA : DT_NULL, .d_un.d_ptr = rela },
        { .d_tag = relocations != 0 ? DT_RELASZ : DT_NULL, .d_un.d_val = relocations * sizeof(Elf64_Rela) },
        { .d_tag = relocations != 0 ? DT_RELAENT : DT_NULL, .d_un.d_val = sizeof(Elf64_Rela) },
        { .d_tag = relative != 0 ? DT_RELACOUNT : DT_NULL, .d_un.d_val = relative },
        { .d_tag = totals->relr_words != 0 ? DT_RELR : DT_NULL, .d_un.d_ptr = relr },
        { .d_tag = totals->relr_words != 0 ? DT_RELRSZ : DT_NULL, .d_un.d_val = totals->relr_words * sizeof(uint64_t) },
        { .d_tag = totals->relr_words != 0 ? DT_RELRENT : DT_NULL, .d_un.d_val = sizeof(uint64_t) },
        { .d_tag = DT_SYMTAB, .d_un.d_ptr = dynsym },
        { .d_tag = DT_SYMENT, .d_un.d_val = sizeof(Elf64_Sym) },
        { .d_tag = DT_STRTAB, .d_un.d_ptr = dynstr },
        { .d_tag = DT_STRSZ, .d_un.d_val = totals->linker_bytes[EXECUTABLE_SECTION_DYNSTR] },
        { .d_tag = DT_GNU_HASH, .d_un.d_ptr = gnu_hash },
        { .d_tag = totals->has_init_array ? DT_INIT_ARRAY : DT_NULL, .d_un.d_ptr = init_array != 0 ? base + init_array->address : 0 },
        { .d_tag = totals->has_init_array ? DT_INIT_ARRAYSZ : DT_NULL, .d_un.d_val = init_array != 0 ? init_array->size : 0 },
        { .d_tag = totals->has_fini_array ? DT_FINI_ARRAY : DT_NULL, .d_un.d_ptr = fini_array != 0 ? base + fini_array->address : 0 },
        { .d_tag = totals->has_fini_array ? DT_FINI_ARRAYSZ : DT_NULL, .d_un.d_val = fini_array != 0 ? fini_array->size : 0 },
        { .d_tag = totals->has_init ? DT_INIT : DT_NULL, .d_un.d_ptr = init },
        { .d_tag = totals->has_fini ? DT_FINI : DT_NULL, .d_un.d_ptr = fini },
        { .d_tag = totals->versions != 0 ? DT_VERSYM : DT_NULL, .d_un.d_ptr = versym },
        { .d_tag = totals->versions != 0 ? DT_VERNEED : DT_NULL, .d_un.d_ptr = verneed },
        { .d_tag = totals->versions != 0 ? DT_VERNEEDNUM : DT_NULL, .d_un.d_val = totals->verneed_files },
    };
    // The entries that apply, in order, then DT_NULL.
    uint32_t count = 0;
    for (uint32_t i = 0; i < BUSTER_EXECUTABLE_MAX_DYNAMIC_ENTRIES; i += 1)
    {
        if (all[i].d_tag != DT_NULL && entries != 0)
        {
            entries[count] = all[i];
        }
        count += all[i].d_tag != DT_NULL ? 1 : 0;
    }
    if (entries != 0)
    {
        entries[count] = (Elf64_Dyn){ .d_tag = DT_NULL };
    }
    return count + 1;
}

// The GNU hash of a name (.gnu.hash's): h = h * 33 + c from 5381, per byte.
static uint32_t executable_gnu_hash(const uint8_t* bytes, uint64_t length)
{
    uint32_t hash = 5381;
    for (uint64_t i = 0; i < length; i += 1)
    {
        hash = hash * 33 + bytes[i];
    }
    return hash;
}

// A PIE's totals, the same on every lane, from the lanes' counts: what its sections hold, and their sizes for the
// layout. .gnu.hash has a bucket per four exports, at least one, and a Bloom filter of 12 bits per export in the next
// power of two of 64-bit words above them, as lld sizes them. A static PIE (with the static runtime) has no interpreter,
// library, versions or exports, and its IRELATIVE relocations in .rela.dyn: one for each stub's slot and for each
// IFUNC's GOT slot without one.
static void executable_dynamic_totals(const Link* link, const ExecutableShared* shared, const ExecutableDynamic* dynamic, uint32_t lanes, ExecutableDynamicTotals* totals)
{
    *totals = (ExecutableDynamicTotals){ 0 };
    uint64_t name_bytes = 0;
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        const ExecutableDynamicCounts* counts = &shared->dynamic_counts[lane];
        totals->stubs += counts->stubs;
        totals->undefined += counts->symbols;
        name_bytes += counts->name_bytes;
        totals->got_relative += counts->got_relative;
        totals->got_symbolic += counts->got_symbolic;
        totals->abs64_relative += counts->abs64_relative;
        totals->abs64_symbolic += counts->abs64_symbolic;
        totals->relr_relocations += counts->relr_relocations;
        totals->relr_words += counts->relr_words;
        totals->irelatives += shared->runtime ? counts->irelatives : 0;
        totals->has_init_array = totals->has_init_array || shared->counts[lane].bytes[executable_bucket(BUSTER_LINK_CLASS_INIT_ARRAY, BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2)] != 0;
        totals->has_fini_array = totals->has_fini_array || shared->counts[lane].bytes[executable_bucket(BUSTER_LINK_CLASS_FINI_ARRAY, BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2)] != 0;
    }
    uint64_t version_name_bytes = 0;
    for (uint32_t v = 0; v < dynamic->version_count; v += 1)
    {
        const ExecutableVersion* version = &dynamic->versions[v];
        const bool used = shared->version_numbers[version->index] != 0;
        totals->versions += used ? 1 : 0;
        totals->library_versions[version->library] += used ? 1 : 0;
        version_name_bytes += used ? version->name.length + 1 : 0;
    }
    for (uint32_t l = 0; l < BUSTER_EXECUTABLE_LIBRARIES; l += 1)
    {
        totals->needed[l] = dynamic->needed[l].length != 0 && (l == 0 || totals->library_versions[l] != 0);
        totals->verneed_files += totals->library_versions[l] != 0 ? 1 : 0;
    }
    uint64_t export_bytes = 0;
    for (uint32_t e = 0; e < dynamic->export_count; e += 1)
    {
        export_bytes += (uint64_t)link->names.length[link->symbols.name[dynamic->exports[e]]] + 1;
    }
    totals->irelatives += shared->runtime ? totals->stubs : 0;
    totals->has_init = dynamic->init != 0;
    totals->has_fini = dynamic->fini != 0;
    totals->exports = dynamic->export_count;
    totals->export_names = 1 + name_bytes;
    totals->needed_names[0] = totals->export_names + export_bytes;
    totals->needed_names[1] = totals->needed_names[0] + (totals->needed[0] ? dynamic->needed[0].length + 1 : 0);
    totals->version_names = totals->needed_names[1] + (totals->needed[1] ? dynamic->needed[1].length + 1 : 0);
    totals->buckets = totals->exports / 4 > 1 ? totals->exports / 4 : 1;
    uint64_t words = 1;
    while (totals->exports != 0 && words <= totals->exports * 12 / 64)
    {
        words *= 2;
    }
    totals->bloom_words = words;
    const uint64_t entries = 1 + totals->undefined + totals->exports;
    uint64_t* bytes = totals->linker_bytes;
    bytes[EXECUTABLE_SECTION_INTERP] = dynamic->interpreter.length != 0 ? dynamic->interpreter.length + 1 : 0;
    bytes[EXECUTABLE_SECTION_ABI_NOTE] = dynamic->note.length;
    bytes[EXECUTABLE_SECTION_DYNSYM] = entries * sizeof(Elf64_Sym);
    bytes[EXECUTABLE_SECTION_VERSYM] = totals->versions != 0 ? entries * sizeof(Elf64_Half) : 0;
    bytes[EXECUTABLE_SECTION_VERNEED] = totals->verneed_files * sizeof(Elf64_Verneed) + totals->versions * sizeof(Elf64_Vernaux);
    bytes[EXECUTABLE_SECTION_GNU_HASH] = 16 + 8 * totals->bloom_words + 4 * totals->buckets + 4 * totals->exports;
    bytes[EXECUTABLE_SECTION_DYNSTR] = totals->version_names + version_name_bytes;
    bytes[EXECUTABLE_SECTION_RELA_DYN] = (totals->got_relative + totals->got_symbolic + totals->abs64_relative + totals->abs64_symbolic + totals->irelatives) * sizeof(Elf64_Rela);
    bytes[EXECUTABLE_SECTION_RELR_DYN] = totals->relr_words * sizeof(uint64_t);
    bytes[EXECUTABLE_SECTION_PLT_GOT] = totals->stubs * BUSTER_EXECUTABLE_STUB_BYTES;
    totals->dynamic_entries = executable_dynamic_entries(0, totals, 0, 0, 0);
    bytes[EXECUTABLE_SECTION_DYNAMIC] = totals->dynamic_entries * sizeof(Elf64_Dyn);
}

// Numbers the versions that the imports need, from 2 in the order of ExecutableDynamic.versions (an executable's own
// are 0 and 1), from every lane's bitset, and with RELR GLIBC_ABI_DT_RELR; 0 for a version nothing needs.
static void executable_number_versions(ExecutableShared* shared, const ExecutableDynamic* dynamic, uint32_t lanes)
{
    memset(shared->version_numbers, 0, shared->version_limit * sizeof(uint16_t));
    uint16_t number = 2;
    for (uint32_t v = 0; v < dynamic->version_count; v += 1)
    {
        const uint16_t index = dynamic->versions[v].index;
        uint64_t used = shared->relr && index == dynamic->relr_version ? 1 : 0;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            used |= shared->versions_used[lane * shared->version_words + index / 64] >> (index % 64) & 1;
        }
        // An index given twice is numbered once.
        const bool numbered = used != 0 && index > VER_NDX_GLOBAL && shared->version_numbers[index] == 0;
        shared->version_numbers[index] = numbered ? number : shared->version_numbers[index];
        number += numbered ? 1 : 0;
    }
}

// Fills this lane's atoms' addresses and pieces, and its range of the symbols' GOT slots and, in a dynamic PIE or a
// static executable with the C runtime, the references of the symbols that have stubs (their stubs' addresses), from
// where the layout puts its buckets, its slots and its stubs: after the lanes' before it. An atom of an ordered class
// whose place isn't a multiple of its alignment fails in `error`.
static void executable_place(Link* link, ExecutableShared* shared, const ExecutableLayout* layout, uint32_t lane, ExecutableLaneError* error)
{
    uint64_t addresses[BUSTER_EXECUTABLE_BUCKETS];
    uint64_t pieces[BUSTER_EXECUTABLE_BUCKETS];
    for (uint32_t bucket = 0; bucket < BUSTER_EXECUTABLE_BUCKETS; bucket += 1)
    {
        addresses[bucket] = layout->bucket_address[bucket];
        pieces[bucket] = layout->bucket_piece[bucket];
        for (uint32_t other = 0; other < lane; other += 1)
        {
            addresses[bucket] += shared->counts[other].bytes[bucket];
            pieces[bucket] += shared->counts[other].pieces[bucket];
        }
    }
    // As executable_count goes: the columns in locals, and a run of atoms of one bucket placed from registers.
    LinkAtoms* atoms = &link->atoms;
    const uint8_t* class_of = atoms->class;
    const uint8_t* alignment_of = atoms->alignment_log2;
    const uint64_t* size_of = atoms->size;
    uint8_t* const* bytes_of = atoms->bytes;
    uint64_t* address_of = atoms->address;
    ExecutablePiece* placed = shared->pieces;
    uint32_t run = 0;
    uint64_t run_address = addresses[0];
    uint64_t run_piece = pieces[0];
    const Range64 atom_range = lane_range(atoms->count - 1);
    const AtomId atom_end = (AtomId)atom_range.end + 1;
    // The range's prioritized atoms, as executable_count meets them, which executable_place_prioritized places.
    const LinkPriorities* priorities = &link->priorities;
    uint32_t priority = executable_first_priority(priorities, (AtomId)atom_range.begin + 1);
    // A block of 16 that executable_atom_block takes, whose atoms' bytes are either all in the file or none, is placed
    // in vectors: each atom's address is the run's plus the padded sizes before it (prefix sums), and its piece's file
    // offset that address's place in the block's section. Another block (or with `exact`, every one) goes one atom at a
    // time.
    const __m512i lane_ids = _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 6, 7);
    for (AtomId block = (AtomId)atom_range.begin + 1; block < atom_end; block += 16)
    {
        const AtomId block_end = atom_end - block > 16 ? block + 16 : atom_end;
        const __mmask16 lanes = executable_block_lanes(block, atom_end);
        ExecutableAtomBlock taken = shared->exact ? (ExecutableAtomBlock){ .taken = false } : executable_atom_block(atoms, block, lanes);
        taken.taken = taken.taken && (taken.in_file == lanes || taken.in_file == 0);
        if (taken.taken)
        {
            if (taken.bucket != run)
            {
                addresses[run] = run_address;
                pieces[run] = run_piece;
                run = taken.bucket;
                run_address = addresses[run];
                run_piece = pieces[run];
            }
            const uint8_t section = executable_class_section((uint8_t)(taken.bucket / BUSTER_EXECUTABLE_ALIGNMENTS + 1), layout->crt);
            const uint64_t delta = layout->sections[section].offset - layout->sections[section].address;
            const uint32_t count = block_end - block;
            for (uint32_t half = 0; half < 2 && 8 * half < count; half += 1)
            {
                const __mmask8 half_lanes = (__mmask8)(lanes >> (8 * half));
                const __m512i inclusive = executable_scan64(taken.padded[half]);
                const __m512i address = _mm512_sub_epi64(_mm512_add_epi64(_mm512_set1_epi64((long long)run_address), inclusive), taken.padded[half]);
                _mm512_mask_storeu_epi64(address_of + block + 8 * half, half_lanes, address);
                if (taken.in_file != 0)
                {
                    const __m512i bytes = _mm512_maskz_loadu_epi64(half_lanes, bytes_of + block + 8 * half);
                    const __m512i ids = _mm512_add_epi64(_mm512_set1_epi64((long long)(block + 8 * half)), lane_ids);
                    const uint32_t records = count - 8 * half >= 8 ? 8 : count - 8 * half;
                    executable_store_pieces(placed + run_piece, _mm512_add_epi64(address, _mm512_set1_epi64((long long)delta)), taken.sizes[half], bytes, ids, records);
                    run_piece += records;
                }
                run_address += executable_last64(inclusive);
            }
        }
        for (AtomId atom = taken.taken ? block_end : block; atom < block_end; atom += 1)
        {
            const uint8_t class = class_of[atom];
            const uint64_t size = size_of[atom];
            const uint32_t log2 = executable_alignment_log2(class, alignment_of[atom], size, layout->crt);
            const uint32_t bucket = executable_bucket(class, log2);
            const bool prioritized = priority < priorities->count && priorities->atom[priority] == atom;
            priority += prioritized ? 1 : 0;
            if (bucket != run && !prioritized)
            {
                addresses[run] = run_address;
                pieces[run] = run_piece;
                run = bucket;
                run_address = addresses[bucket];
                run_piece = pieces[bucket];
            }
            // An ordered class's atoms follow one another without padding, so each must land on a multiple of its
            // alignment (the others' buckets keep theirs); .eh_frame's records need only 4 bytes whatever their atoms'
            // alignment, as lld writes them one after another.
            const uint32_t required = class == BUSTER_LINK_CLASS_EH_FRAME && alignment_of[atom] > 2 ? 2 : alignment_of[atom];
            if (executable_class_is_ordered(class, layout->crt) && (run_address & ((1ull << required) - 1)) != 0)
            {
                executable_lane_fail(error, EXECUTABLE_ERROR_LAYOUT, atom, 0, 0);
            }
            if (!prioritized)
            {
                address_of[atom] = run_address;
            }
            if (!prioritized && executable_in_file(class, size))
            {
                const uint8_t section = executable_class_section(class, layout->crt);
                placed[run_piece] = (ExecutablePiece){
                    .offset = executable_file_offset(layout, section, run_address),
                    .size = size,
                    .bytes = bytes_of[atom],
                    .atom = atom,
                };
                run_piece += 1;
            }
            run_address += prioritized ? 0 : executable_align(size, 1ull << log2);
        }
    }

    uint32_t slot = 1;
    uint64_t stub = 0;
    const bool stubbed = layout->dynamic || layout->runtime;
    for (uint32_t other = 0; other < lane; other += 1)
    {
        slot += (uint32_t)shared->got_counts[other];
        stub += stubbed ? shared->dynamic_counts[other].stubs : 0;
    }
    const uint64_t stubs = layout->sections[EXECUTABLE_SECTION_PLT_GOT].address;
    const uint64_t count = link->symbols.count;
    const __m512i steps = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const Range64 words = lane_range(shared->words_per_lane);
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        // Each marked symbol's slot, one after another, and 0 for the others: 16 symbols at a time, the next slots
        // expanded into the marked lanes. With `exact`, its twin: one symbol at a time.
        const uint64_t first = 64 * word;
        const uint64_t marks = shared->got_marks[word];
        for (uint32_t part = 0; !shared->exact && part < 4; part += 1)
        {
            const uint64_t from = first + 16 * part;
            const __mmask16 valid = from < count ? (__mmask16)(count - from >= 16 ? 0xffff : (1u << (count - from)) - 1) : 0;
            const __mmask16 marked = (__mmask16)(marks >> (16 * part));
            const __m512i slots = _mm512_maskz_expand_epi32(marked, _mm512_add_epi32(_mm512_set1_epi32((int)slot), steps));
            _mm512_mask_storeu_epi32(link->symbols.got_slot + from, valid, slots);
            slot += (uint32_t)__builtin_popcount(marked);
        }
        for (uint64_t bit = 0; shared->exact && bit < 64 && first + bit < count; bit += 1)
        {
            const bool marked = (marks >> bit & 1) != 0;
            link->symbols.got_slot[first + bit] = marked ? slot : 0;
            slot += marked ? 1 : 0;
        }
        for (uint64_t left = stubbed ? shared->stub_marks[word] : 0; left != 0; left &= left - 1)
        {
            link->symbols.reference[first + (uint64_t)__builtin_ctzll(left)] = (uint32_t)(stubs + BUSTER_EXECUTABLE_STUB_BYTES * stub);
            stub += 1;
        }
    }
}

// A dynamic PIE's pieces of the atoms the loader writes, with .dynamic's and .got's among them, which .relr.dyn's words
// are of: from .tdata's first bucket's to .bss's (zero-fill, without pieces).
static Range64 executable_written_pieces(const ExecutableLayout* layout)
{
    const Range64 result = {
        .begin = layout->bucket_piece[executable_bucket(BUSTER_LINK_CLASS_TDATA, BUSTER_EXECUTABLE_PAGE_LOG2)],
        .end = layout->bucket_piece[executable_bucket(BUSTER_LINK_CLASS_BSS, BUSTER_EXECUTABLE_PAGE_LOG2)],
    };
    return result;
}

// Lane 0: the prioritized init and fini array atoms (link->priorities), which the lanes' runs leave out. Each array's go
// first in its section, as GNU ld and lld put .init_array.N and .fini_array.N before .init_array and .fini_array: by
// priority, and in id order for equal ones (a stable LSD radix sort of the entries, 8 bits a pass over the array's
// index above the priority), one after another from ExecutableLayout.prioritized_address, with their pieces from
// prioritized_piece. One lane sorts them: a program has a few (two of the static runtime's 2,622 members have one, and
// each constructor a program gives a priority). Returns false if the arena has no room for the sort.
static bool executable_place_prioritized(Link* link, const ExecutableLayout* layout, ExecutablePiece* placed, Arena* arena)
{
    const LinkPriorities* priorities = &link->priorities;
    const LinkAtoms* atoms = &link->atoms;
    const uint32_t count = priorities->count > 1 ? priorities->count - 1 : 0;
    uint32_t* entries = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * (uint64_t)count) : 0;
    const bool result = count == 0 || entries != 0;
    uint32_t* from = entries;
    uint32_t* to = entries != 0 ? entries + count : 0;
    for (uint32_t e = 0; result && e < count; e += 1)
    {
        from[e] = e + 1;
    }
    for (uint32_t pass = 0; result && count != 0 && pass < 3; pass += 1)
    {
        uint32_t starts[257] = { 0 };
        const uint32_t shift = 8 * pass;
        for (uint32_t e = 0; e < count; e += 1)
        {
            const uint32_t key = executable_array_index(atoms->class[priorities->atom[from[e]]]) << 16 | priorities->priority[from[e]];
            starts[(key >> shift & 0xff) + 1] += 1;
        }
        for (uint32_t digit = 0; digit < 256; digit += 1)
        {
            starts[digit + 1] += starts[digit];
        }
        for (uint32_t e = 0; e < count; e += 1)
        {
            const uint32_t key = executable_array_index(atoms->class[priorities->atom[from[e]]]) << 16 | priorities->priority[from[e]];
            to[starts[key >> shift & 0xff]] = from[e];
            starts[key >> shift & 0xff] += 1;
        }
        uint32_t* swapped = from;
        from = to;
        to = swapped;
    }
    uint64_t address[2] = {
        layout->prioritized_address[0],
        layout->prioritized_address[1],
    };
    uint64_t piece[2] = {
        layout->prioritized_piece[0],
        layout->prioritized_piece[1],
    };
    for (uint32_t e = 0; result && e < count; e += 1)
    {
        const AtomId atom = priorities->atom[from[e]];
        const uint8_t class = atoms->class[atom];
        const uint32_t array = executable_array_index(class);
        atoms->address[atom] = address[array];
        if (executable_in_file(class, atoms->size[atom]))
        {
            placed[piece[array]] = (ExecutablePiece){
                .offset = executable_file_offset(layout, executable_class_section(class, layout->crt), address[array]),
                .size = atoms->size[atom],
                .bytes = atoms->bytes[atom],
                .atom = atom,
            };
            piece[array] += 1;
        }
        address[array] += atoms->size[atom];
    }
    return result;
}

// The 16 bits of a bitset of `words` words from bit `first` (as many as it has).
static uint32_t executable_bits16(const uint64_t* bits, uint64_t words, uint64_t first)
{
    const uint64_t word = first / 64;
    const uint32_t shift = (uint32_t)(first % 64);
    const uint64_t low = word < words ? bits[word] >> shift : 0;
    const uint64_t high = shift > 48 && word + 1 < words ? bits[word + 1] << (64 - shift) : 0;
    return (uint32_t)((low | high) & 0xffff);
}

// A stub at image offset `stub`, in the stubs' bytes: jmp [rip + its GOT slot], from the end of its 6 bytes, and a
// two-byte nop, as GNU ld's .plt.got entries are.
static void executable_write_stub(uint8_t* stubs, const ExecutableLayout* layout, uint32_t stub, uint32_t slot)
{
    const uint64_t stubs_address = layout->sections[EXECUTABLE_SECTION_PLT_GOT].address;
    const uint64_t got_address = layout->sections[EXECUTABLE_SECTION_GOT].address;
    uint8_t* bytes = stubs + (stub - stubs_address);
    const int32_t distance = (int32_t)(got_address + 8 * (uint64_t)slot - ((uint64_t)stub + 6));
    bytes[0] = 0xff;
    bytes[1] = 0x25;
    memcpy(bytes + 2, &distance, sizeof(distance));
    bytes[6] = 0x66;
    bytes[7] = 0x90;
}

// Fills this lane's range of the symbols' references and their GOT slots, and of the chunks' boundaries. Most symbols
// are in an atom that isn't thread-local, whose reference comes from the columns directly (in locals, which the stores
// to the references can't alias); executable_reference gives the others', and in a static executable, the lane's
// lowest and highest reference of an absolute symbol, for the patch's range proof. A static executable's IFUNCs (with
// the C runtime) go one at a time: one with a stub keeps its reference (the stub's, from executable_place) and gets the
// stub's bytes, and the GOT slot of each stays 0, which an IRELATIVE relocation fills at startup. A dynamic PIE's SYMBOLIC
// symbols come after, by this lane's range of the bitsets' words (as executable_mark_symbols counts them): their stubs,
// their references (a stub's address from executable_place, or address 0) and their .dynsym entries, names and
// versions, after the lanes' before them, with their indices; the other symbols' indices are 0. Then the RELR words of
// this lane's range of the written pieces (executable_written_pieces) before and after the GOT's. Failures go to
// `error`.
static void executable_resolve(Link* link, ExecutableShared* shared, const ExecutableLayout* layout, uint32_t lane, ExecutableLaneError* error)
{
    LinkSymbols* symbols = &link->symbols;
    const bool dynamic = layout->dynamic;
    const AtomId* atom_of = symbols->atom;
    const uint64_t* value_of = symbols->value;
    const uint32_t* slot_of = symbols->got_slot;
    uint32_t* reference_of = symbols->reference;
    const uint8_t* class_of = link->atoms.class;
    const uint64_t* address_of = link->atoms.address;
    uint8_t* got = shared->got;
    uint8_t* stub_bytes = shared->section_bytes[EXECUTABLE_SECTION_PLT_GOT];
    const bool runtime = layout->runtime;
    const uint64_t base = layout->base;
    const Range64 symbol_range = lane_range(symbols->count - 1);
    const SymbolId symbol_end = (SymbolId)symbol_range.end + 1;
    int64_t absolute_low = INT64_MAX;
    int64_t absolute_high = INT64_MIN;
    // 16 symbols at a time whose atoms are all defined and not thread-local, as most are: their classes and addresses
    // by gathers, their references and GOT slots in vectors, and a slot's value written one at a time. The class gather
    // reads 4 bytes, so an atom must be 3 below the end of the class column (and its id fit a gather's signed index).
    // Another block (or with `exact`, every one) goes one symbol at a time.
    const uint64_t gather_limit = link->atoms.capacity > 3 ? link->atoms.capacity - 3 : 0;
    const __m512i limit = _mm512_set1_epi32((int)(gather_limit < INT32_MAX ? gather_limit : INT32_MAX));
    for (SymbolId block = (SymbolId)symbol_range.begin + 1; block < symbol_end; block += 16)
    {
        const SymbolId block_end = symbol_end - block > 16 ? block + 16 : symbol_end;
        const __mmask16 lanes = executable_block_lanes(block, symbol_end);
        bool taken = false;
        if (!shared->exact)
        {
            const __m512i atoms = _mm512_maskz_loadu_epi32(lanes, atom_of + block);
            const __mmask16 defined = _mm512_mask_cmpneq_epi32_mask(lanes, atoms, _mm512_setzero_si512()) & _mm512_mask_cmplt_epu32_mask(lanes, atoms, limit);
            const __m512i classes = _mm512_and_si512(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), defined, atoms, class_of, 1), _mm512_set1_epi32(0xff));
            const __mmask16 tls = _mm512_mask_cmpeq_epi32_mask(defined, classes, _mm512_set1_epi32(BUSTER_LINK_CLASS_TDATA)) |
                                  _mm512_mask_cmpeq_epi32_mask(defined, classes, _mm512_set1_epi32(BUSTER_LINK_CLASS_TBSS));
            const uint32_t ifuncs = runtime ? executable_bits16(shared->ifunc_bits, shared->words_per_lane, block) & lanes : 0;
            taken = defined == lanes && tls == 0 && ifuncs == 0;
            if (taken)
            {
                __m256i references[2];
                for (uint32_t half = 0; half < 2; half += 1)
                {
                    const __mmask8 half_lanes = (__mmask8)(lanes >> (8 * half));
                    const __m256i half_atoms = half == 0 ? _mm512_castsi512_si256(atoms) : _mm512_extracti64x4_epi64(atoms, 1);
                    const __m512i addresses = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), half_lanes, half_atoms, address_of, 8);
                    const __m512i values = _mm512_maskz_loadu_epi64(half_lanes, value_of + block + 8 * half);
                    references[half] = _mm512_cvtepi64_epi32(_mm512_add_epi64(addresses, values));
                }
                const __m512i reference = _mm512_inserti64x4(_mm512_castsi256_si512(references[0]), references[1], 1);
                _mm512_mask_storeu_epi32(reference_of + block, lanes, reference);
                const __mmask16 slotted = _mm512_mask_test_epi32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, slot_of + block), _mm512_set1_epi32(-1));
                for (uint32_t left = slotted; left != 0; left &= left - 1)
                {
                    const SymbolId symbol = block + (SymbolId)__builtin_ctz(left);
                    const int64_t value = (int64_t)base + (int32_t)reference_of[symbol];
                    memcpy(got + 8 * (uint64_t)slot_of[symbol], &value, sizeof(value));
                }
            }
        }
        for (SymbolId symbol = taken ? block_end : block; symbol < block_end; symbol += 1)
        {
            const AtomId atom = atom_of[symbol];
            const bool plain = atom != 0 && !link_class_is_tls(class_of[atom]);
            const bool symbolic = !plain && dynamic && executable_bit(shared->dynamic_bits, symbol);
            const bool ifunc = runtime && executable_bit(shared->ifunc_bits, symbol);
            const bool stubbed = ifunc && executable_bit(shared->stub_marks, symbol);
            // __tls_get_addr, whose calls the TLS accesses' relaxation consumes, refers to nothing.
            const bool relaxed = symbol == shared->relaxed;
            uint32_t reference = plain ? (uint32_t)(address_of[atom] + value_of[symbol]) : 0;
            reference = stubbed ? reference_of[symbol] : reference;
            const ExecutableError failure = plain || symbolic || relaxed ? EXECUTABLE_ERROR_NONE : executable_reference(link, layout, symbol, &reference);
            if (failure != EXECUTABLE_ERROR_NONE)
            {
                executable_lane_fail(error, failure, 0, symbol, 0);
            }
            const bool absolute = !plain && !dynamic && symbols->source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE;
            absolute_low = absolute && (int32_t)reference < absolute_low ? (int32_t)reference : absolute_low;
            absolute_high = absolute && (int32_t)reference > absolute_high ? (int32_t)reference : absolute_high;
            if (stubbed)
            {
                const uint64_t stub = (reference - layout->sections[EXECUTABLE_SECTION_PLT_GOT].address) / BUSTER_EXECUTABLE_STUB_BYTES;
                executable_write_stub(stub_bytes, layout, reference, (uint32_t)(layout->stub_slot + stub));
            }
            if (!symbolic)
            {
                reference_of[symbol] = reference;
                const uint32_t slot = slot_of[symbol];
                if (slot != 0 && (!ifunc || stubbed))
                {
                    // A TLS symbol's slot holds its offset from the thread pointer (TPOFF64), another's its address: in
                    // a dynamic PIE, the address at base 0, which a RELATIVE relocation's addend repeats.
                    const int64_t value = !plain && executable_is_tls(link, symbol) ? (int32_t)reference : (int64_t)base + (int32_t)reference;
                    memcpy(got + 8 * (uint64_t)slot, &value, sizeof(value));
                }
            }
        }
    }
    shared->absolute_bounds[2 * lane] = absolute_low;
    shared->absolute_bounds[2 * lane + 1] = absolute_high;
    uint64_t entry = 1;
    uint64_t name_offset = 1;
    for (uint32_t other = 0; dynamic && other < lane; other += 1)
    {
        entry += shared->dynamic_counts[other].symbols;
        name_offset += shared->dynamic_counts[other].name_bytes;
    }
    Elf64_Sym* dynsym = (Elf64_Sym*)shared->section_bytes[EXECUTABLE_SECTION_DYNSYM];
    Elf64_Half* versym = (Elf64_Half*)shared->section_bytes[EXECUTABLE_SECTION_VERSYM];
    uint8_t* dynstr = shared->section_bytes[EXECUTABLE_SECTION_DYNSTR];
    const Range64 words = lane_range(dynamic ? shared->words_per_lane : 0);
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        const uint64_t first = 64 * word;
        for (uint32_t part = 0; part < 4; part += 1)
        {
            const uint64_t from = first + 16 * part;
            const __mmask16 lanes = from < symbols->count ? (__mmask16)(symbols->count - from >= 16 ? 0xffff : (1u << (symbols->count - from)) - 1) : 0;
            _mm512_mask_storeu_epi32(symbols->dynamic_index + from, lanes, _mm512_setzero_si512());
        }
        for (uint64_t left = shared->dynamic_bits[word]; left != 0; left &= left - 1)
        {
            // A stub's address, or address 0 (its fixups reach it through the GOT or the loader). Its GOT slot holds 0
            // (executable_prepare's), which the loader replaces.
            const SymbolId symbol = (SymbolId)(first + (uint64_t)__builtin_ctzll(left));
            const bool stubbed = (shared->stub_marks[word] >> (symbol % 64) & 1) != 0;
            const uint32_t reference = stubbed ? symbols->reference[symbol] : (uint32_t)(0 - layout->base);
            symbols->reference[symbol] = reference;
            const uint32_t slot = symbols->got_slot[symbol];
            if (stubbed)
            {
                executable_write_stub(stub_bytes, layout, reference, slot);
            }
            // Undefined, at its binding and type with default visibility; versioned at its import's version (the
            // executable's number for it), as unversioned for an import without one, or for a weak reference to
            // nothing as local (lld's 0).
            const bool import = symbols->source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED;
            const NameId name = symbols->name[symbol];
            const uint64_t length = link->names.length[name];
            memcpy(dynstr + name_offset, link->names.bytes[name], length);
            dynstr[name_offset + length] = 0;
            dynsym[entry] = (Elf64_Sym){
                .st_name = (uint32_t)name_offset,
                .st_info = (uint8_t)(symbols->flags[symbol] & (BUSTER_LINK_SYMBOL_BINDING | BUSTER_LINK_SYMBOL_TYPE)),
                .st_other = (uint8_t)(symbols->flags[symbol] >> 6),
                .st_shndx = SHN_UNDEF,
            };
            const uint16_t version = import ? symbols->version[symbol] : 0;
            const uint16_t number = version < shared->version_limit ? shared->version_numbers[version] : 0;
            if (versym != 0)
            {
                versym[entry] = import ? (number != 0 ? number : VER_NDX_GLOBAL) : VER_NDX_LOCAL;
            }
            symbols->dynamic_index[symbol] = (uint32_t)entry;
            entry += 1;
            name_offset += length + 1;
        }
    }
    const Range64 boundary_range = lane_range(layout->chunk_count - 1);
    for (uint64_t chunk = boundary_range.begin + 1; chunk < boundary_range.end + 1; chunk += 1)
    {
        shared->boundaries[chunk] = executable_chunk_boundary(link, shared->pieces, layout->piece_count, chunk * BUSTER_EXECUTABLE_CHUNK_BYTES, shared->relax);
    }
    uint64_t before = 0;
    uint64_t after = 0;
    const uint64_t got_piece = layout->section_piece[EXECUTABLE_SECTION_GOT];
    const Range64 written = executable_written_pieces(layout);
    const Range64 piece_range = lane_range(dynamic && shared->relr ? written.end - written.begin : 0);
    for (uint64_t piece = written.begin + piece_range.begin; piece < written.begin + piece_range.end; piece += 1)
    {
        const AtomId atom = shared->pieces[piece].atom;
        const uint64_t words_of_atom = atom != 0 ? shared->relr_words[atom] & BUSTER_EXECUTABLE_RELR_WORDS : 0;
        // A link without a GOT has its piece 0, the headers', before every atom.
        before += piece < got_piece ? words_of_atom : 0;
        after += piece < got_piece ? 0 : words_of_atom;
    }
    if (dynamic)
    {
        shared->dynamic_counts[lane].relr_before_got = before;
        shared->dynamic_counts[lane].relr_after_got = after;
    }
}

// The static runtime's IRELATIVE relocations, at `rela`: a static executable's .rela.iplt, or the end of a static PIE's
// .rela.dyn. In slot order: this lane's range of the GOT's words, the GOT slots of its IFUNCs without stubs
// (executable_ifunc_slots), then the stubs' slots, each after the lanes' before it. An IRELATIVE relocation's addend is
// the IFUNC's own address, its resolver's, which libc.a's startup calls to fill the slot: a static executable's between
// __rela_iplt_start and __rela_iplt_end, and a static PIE's as it relocates itself, after the others in .rela.dyn.
static void executable_irelative_relocations(const Link* link, ExecutableShared* shared, const ExecutableLayout* layout, Elf64_Rela* rela, uint32_t lanes, uint32_t lane)
{
    uint64_t index = 0;
    uint64_t stub_index = 0;
    uint64_t before_stubs = 0;
    for (uint32_t other = 0; other < lanes; other += 1)
    {
        index += other < lane ? shared->dynamic_counts[other].irelatives : 0;
        stub_index += other < lane ? shared->dynamic_counts[other].stubs : 0;
        before_stubs += shared->dynamic_counts[other].irelatives;
    }
    const uint64_t got = layout->base + layout->sections[EXECUTABLE_SECTION_GOT].address;
    const Range64 words = lane_range(shared->words_per_lane);
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        for (uint64_t left = executable_ifunc_slots(link, shared, word, shared->got_marks[word]); left != 0; left &= left - 1)
        {
            const SymbolId symbol = (SymbolId)(64 * word + (uint64_t)__builtin_ctzll(left));
            rela[index] = (Elf64_Rela){
                .r_offset = got + 8 * (uint64_t)link->symbols.got_slot[symbol],
                .r_info = ELF64_R_INFO(0, R_X86_64_IRELATIVE),
                .r_addend = (int64_t)(layout->base + link->atoms.address[link->symbols.atom[symbol]] + link->symbols.value[symbol]),
            };
            index += 1;
        }
        for (uint64_t left = shared->stub_marks[word]; left != 0; left &= left - 1)
        {
            const SymbolId symbol = (SymbolId)(64 * word + (uint64_t)__builtin_ctzll(left));
            rela[before_stubs + stub_index] = (Elf64_Rela){
                .r_offset = got + 8 * (layout->stub_slot + stub_index),
                .r_info = ELF64_R_INFO(0, R_X86_64_IRELATIVE),
                .r_addend = (int64_t)(layout->base + link->atoms.address[link->symbols.atom[symbol]] + link->symbols.value[symbol]),
            };
            stub_index += 1;
        }
    }
}

// A dynamic PIE's .rela.dyn: this lane's range of the GOT's words (the slots the loader relocates, in slot order) and
// of the atoms (the ABS64 fixups of the atoms the loader writes that .relr.dyn doesn't hold, in fixup order), each at
// its place after the lanes' before it: RELATIVE relocations first (the GOT's, then the fixups'), then the others
// (likewise), so that DT_RELACOUNT counts the first ones. A RELATIVE relocation's addend is the address at base 0; a
// SYMBOLIC one names its target's .dynsym entry: GLOB_DAT or TPOFF64 for a slot, R_X86_64_64 with the fixup's addend
// for a fixup. The slots by class come from the bitsets, and with RELR only the atoms that executable_classify found
// .rela.dyn relocations in (BUSTER_EXECUTABLE_RELR_RELA) are read again. With `exact`, every written atom's fixups one
// at a time, their targets' atoms loaded.
static void executable_dynamic_relocations(const Link* link, ExecutableShared* shared, const ExecutableLayout* layout, const ExecutableDynamicTotals* totals,
                                           const ExecutableFilter* filter, uint32_t lane)
{
    Elf64_Rela* rela = (Elf64_Rela*)shared->section_bytes[EXECUTABLE_SECTION_RELA_DYN];
    const uint64_t relative_total = totals->got_relative + totals->abs64_relative;
    uint64_t got_relative = 0;
    uint64_t got_symbolic = relative_total;
    uint64_t abs64_relative = totals->got_relative;
    uint64_t abs64_symbolic = relative_total + totals->got_symbolic;
    for (uint32_t other = 0; other < lane; other += 1)
    {
        got_relative += shared->dynamic_counts[other].got_relative;
        got_symbolic += shared->dynamic_counts[other].got_symbolic;
        abs64_relative += shared->dynamic_counts[other].abs64_relative;
        abs64_symbolic += shared->dynamic_counts[other].abs64_symbolic;
    }
    const LinkSymbols* symbols = &link->symbols;
    const uint64_t got = layout->base + layout->sections[EXECUTABLE_SECTION_GOT].address;
    const Range64 words = lane_range(shared->words_per_lane);
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        const uint64_t marks = shared->got_marks[word];
        for (uint64_t left = shared->relr ? 0 : executable_slots_of_class(link, shared, word, marks, BUSTER_EXECUTABLE_RELOCATION_RELATIVE); left != 0; left &= left - 1)
        {
            const SymbolId symbol = (SymbolId)(word * 64 + (uint64_t)__builtin_ctzll(left));
            rela[got_relative] = (Elf64_Rela){
                .r_offset = got + 8 * (uint64_t)symbols->got_slot[symbol],
                .r_info = ELF64_R_INFO(0, R_X86_64_RELATIVE),
                .r_addend = (int64_t)layout->base + (int32_t)symbols->reference[symbol],
            };
            got_relative += 1;
        }
        for (uint64_t left = executable_slots_of_class(link, shared, word, marks, BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC); left != 0; left &= left - 1)
        {
            const SymbolId symbol = (SymbolId)(word * 64 + (uint64_t)__builtin_ctzll(left));
            const uint32_t type = executable_is_tls(link, symbol) ? R_X86_64_TPOFF64 : R_X86_64_GLOB_DAT;
            rela[got_symbolic] = (Elf64_Rela){
                .r_offset = got + 8 * (uint64_t)symbols->got_slot[symbol],
                .r_info = ELF64_R_INFO(symbols->dynamic_index[symbol], type),
                .r_addend = 0,
            };
            got_symbolic += 1;
        }
    }
    const LinkAtoms* atoms = &link->atoms;
    const LinkFixups* fixups = &link->fixups;
    const Range64 range = lane_range(atoms->count - 1);
    const AtomId atoms_end = (AtomId)range.end + 1;
    for (AtomId first = (AtomId)range.begin + 1; first < atoms_end; first += 64)
    {
        const uint64_t remaining = atoms_end - first;
        const uint64_t valid = remaining >= 64 ? ~0ull : (1ull << remaining) - 1;
        for (uint64_t written = executable_written_atoms(atoms, shared, first, valid); written != 0; written &= written - 1)
        {
            const AtomId atom = first + (AtomId)__builtin_ctzll(written);
            const bool skipped = shared->relr && !shared->exact && (shared->relr_words[atom] & BUSTER_EXECUTABLE_RELR_RELA) == 0;
            const FixupId begin = atoms->fixup_begin[atom];
            const FixupId end = skipped ? begin : begin + atoms->fixup_count[atom];
            for (FixupId fixup = begin; fixup < end; fixup += 1)
            {
                const uint8_t kind = fixups->kind[fixup];
                const SymbolId target = fixups->target[fixup];
                const bool outside = shared->exact ? symbols->atom[target] == 0 : executable_filter_test(filter, _mm512_set1_epi32((int)target), 1) != 0;
                const uint8_t class =
                    (kind & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_ABS64 ? executable_abs64_class(link, target, outside, shared->imports) : BUSTER_EXECUTABLE_RELOCATION_NONE;
                const bool packed = shared->relr && executable_relr_eligible(atoms->alignment_log2[atom], fixups->offset[fixup]);
                const int64_t addend = (kind & BUSTER_LINK_FIXUP_WIDE) != 0 ? fixups->wide_addends[(uint32_t)fixups->addend[fixup]] : fixups->addend[fixup];
                const uint64_t place = layout->base + atoms->address[atom] + fixups->offset[fixup];
                if (class == BUSTER_EXECUTABLE_RELOCATION_RELATIVE && !packed)
                {
                    rela[abs64_relative] = (Elf64_Rela){
                        .r_offset = place,
                        .r_info = ELF64_R_INFO(0, R_X86_64_RELATIVE),
                        .r_addend = (int64_t)layout->base + (int32_t)symbols->reference[target] + addend,
                    };
                    abs64_relative += 1;
                }
                else if (class == BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC)
                {
                    rela[abs64_symbolic] = (Elf64_Rela){
                        .r_offset = place,
                        .r_info = ELF64_R_INFO(symbols->dynamic_index[target], R_X86_64_64),
                        .r_addend = addend,
                    };
                    abs64_symbolic += 1;
                }
            }
        }
    }
}

// A dynamic PIE's GOT slots in .relr.dyn: this lane's range of the GOT's words of marks, each word's RELATIVE slots an
// address entry and, with more than one, a bitmap, at `words`. A word's marked symbols have consecutive slots from its
// first one's, so the RELATIVE ones' ranks among them (a pext of the bitsets) are the slots: the lowest the address
// entry's, the others the bitmap's bits, as executable_relr_add sets them. The slots hold their addresses at base 0
// (executable_resolve). With `exact`, its twin: each RELATIVE slot through executable_relr_add.
static void executable_got_relr(const Link* link, const ExecutableShared* shared, const ExecutableLayout* layout, uint64_t* words)
{
    const uint64_t got = layout->base + layout->sections[EXECUTABLE_SECTION_GOT].address;
    const Range64 range = lane_range(shared->words_per_lane);
    uint64_t at = 0;
    for (uint64_t word = range.begin; words != 0 && word < range.end; word += 1)
    {
        const uint64_t marks = shared->got_marks[word];
        const uint64_t relative = executable_slots_of_class(link, shared, word, marks, BUSTER_EXECUTABLE_RELOCATION_RELATIVE);
        if (shared->exact)
        {
            ExecutableRelr relr = {
                .words = words + at,
            };
            for (uint64_t left = relative; left != 0; left &= left - 1)
            {
                const SymbolId symbol = (SymbolId)(word * 64 + (uint64_t)__builtin_ctzll(left));
                executable_relr_add(&relr, got + 8 * (uint64_t)link->symbols.got_slot[symbol]);
            }
            executable_relr_end(&relr);
            at += relr.count;
        }
        else if (relative != 0)
        {
            const uint64_t first_slot = link->symbols.got_slot[word * 64 + (uint64_t)__builtin_ctzll(marks)];
            const uint64_t ranks = _pext_u64(relative, marks);
            const uint64_t lowest = (uint64_t)__builtin_ctzll(ranks);
            // The ranks after the lowest, at most 63 of them.
            const uint64_t rest = ranks >> lowest >> 1;
            words[at] = got + 8 * (first_slot + lowest);
            if (rest != 0)
            {
                words[at + 1] = rest << 1 | 1;
            }
            at += rest != 0 ? 2 : 1;
        }
    }
}

// A dynamic PIE's .relr.dyn, in address order: the atoms' words before the GOT's piece, the GOT's slots' words
// (executable_got_relr), then the atoms' words after it; this lane's ranges of the written pieces
// (executable_written_pieces, in file order, which is address order) and of the GOT's words each at its place within
// its part. A dense atom's words come from its first fixup's address and its fixup count (executable_relr_dense);
// another's are its RELATIVE ABS64 fixups at even addresses encoded again, which the field's bytes give the addend of
// (the patch writes S + A there). With `exact`, every atom's encoded, targets' atoms loaded.
static void executable_relr_relocations(const Link* link, ExecutableShared* shared, const ExecutableLayout* layout, const ExecutableFilter* filter, uint32_t lane,
                                        uint32_t lanes)
{
    uint64_t* words = (uint64_t*)shared->section_bytes[EXECUTABLE_SECTION_RELR_DYN];
    // The words of the atoms before the GOT's piece, the GOT's, then the atoms' after it: each lane's at its place
    // within each.
    uint64_t before = 0;
    uint64_t got_words = 0;
    uint64_t at = 0;
    uint64_t got_at = 0;
    uint64_t after_at = 0;
    for (uint32_t other = 0; other < lanes; other += 1)
    {
        const ExecutableDynamicCounts* counts = &shared->dynamic_counts[other];
        before += counts->relr_before_got;
        got_words += counts->got_relr_words;
        at += other < lane ? counts->relr_before_got : 0;
        got_at += other < lane ? counts->got_relr_words : 0;
        after_at += other < lane ? counts->relr_after_got : 0;
    }
    got_at += before;
    after_at += before + got_words;
    executable_got_relr(link, shared, layout, words != 0 ? words + got_at : 0);
    const uint64_t got_piece = layout->section_piece[EXECUTABLE_SECTION_GOT];
    const LinkAtoms* atoms = &link->atoms;
    const LinkFixups* fixups = &link->fixups;
    const Range64 written = executable_written_pieces(layout);
    const Range64 piece_range = lane_range(written.end - written.begin);
    const uint64_t first_piece = written.begin + piece_range.begin;
    // A range that starts after the GOT's piece is all after it; one that has it moves past the GOT's words there.
    at = first_piece > got_piece ? after_at : at;
    for (uint64_t piece = first_piece; words != 0 && piece < written.begin + piece_range.end; piece += 1)
    {
        at = piece == got_piece ? after_at : at;
        const AtomId atom = shared->pieces[piece].atom;
        const uint32_t marked = atom != 0 ? shared->relr_words[atom] : 0;
        const FixupId begin = atom != 0 ? atoms->fixup_begin[atom] : 0;
        const uint64_t address = atom != 0 ? layout->base + atoms->address[atom] : 0;
        if ((marked & BUSTER_EXECUTABLE_RELR_DENSE) != 0 && !shared->exact)
        {
            at += executable_relr_dense(words + at, address + fixups->offset[begin], atoms->fixup_count[atom]);
        }
        else if ((marked & BUSTER_EXECUTABLE_RELR_WORDS) != 0)
        {
            ExecutableRelr relr = {
                .words = words + at,
            };
            for (FixupId fixup = begin; fixup < begin + atoms->fixup_count[atom]; fixup += 1)
            {
                const SymbolId target = fixups->target[fixup];
                const bool outside = shared->exact ? link->symbols.atom[target] == 0 : executable_filter_test(filter, _mm512_set1_epi32((int)target), 1) != 0;
                const bool abs64 = (fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_ABS64;
                const bool relative = abs64 && executable_abs64_class(link, target, outside, shared->imports) == BUSTER_EXECUTABLE_RELOCATION_RELATIVE;
                if (relative && executable_relr_eligible(atoms->alignment_log2[atom], fixups->offset[fixup]))
                {
                    executable_relr_add(&relr, address + fixups->offset[fixup]);
                }
            }
            executable_relr_end(&relr);
            at += relr.count;
        }
    }
}

// A PIE's sections that lane 0 writes once the symbols are resolved: the exports' .dynsym entries, names and versions
// after the undefined ones, in .gnu.hash's order (by bucket, a counting sort that keeps ExecutableDynamic's order within
// one), their indices, and .gnu.hash; the library's name and the needed versions' after the names in .dynstr, and
// .gnu.version_r; and .dynamic. A static PIE's .dynsym has the null symbol alone, and its .gnu.hash no symbols, as lld
// writes them. Returns false if the arena can't hold the exports' hashes and order.
static bool executable_dynamic_sections(Arena* arena, Link* link, ExecutableShared* shared, const ExecutableLayout* layout, const ExecutableDynamic* dynamic,
                                        const ExecutableDynamicTotals* totals)
{
    const uint32_t count = dynamic->export_count;
    const uint32_t buckets = (uint32_t)totals->buckets;
    uint32_t* hashes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    uint32_t* starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)buckets + 1);
    const bool result = hashes != 0 && order != 0 && starts != 0;
    if (result)
    {
        LinkSymbols* symbols = &link->symbols;
        Elf64_Sym* dynsym = (Elf64_Sym*)shared->section_bytes[EXECUTABLE_SECTION_DYNSYM];
        Elf64_Half* versym = (Elf64_Half*)shared->section_bytes[EXECUTABLE_SECTION_VERSYM];
        uint8_t* dynstr = shared->section_bytes[EXECUTABLE_SECTION_DYNSTR];
        memset(starts, 0, ((uint64_t)buckets + 1) * sizeof(uint32_t));
        for (uint32_t e = 0; e < count; e += 1)
        {
            const NameId name = symbols->name[dynamic->exports[e]];
            hashes[e] = executable_gnu_hash(link->names.bytes[name], link->names.length[name]);
            starts[hashes[e] % buckets + 1] += 1;
        }
        for (uint32_t b = 0; b < buckets; b += 1)
        {
            starts[b + 1] += starts[b];
        }
        for (uint32_t e = 0; e < count; e += 1)
        {
            const uint32_t b = hashes[e] % buckets;
            order[starts[b]] = e;
            starts[b] += 1;
        }

        // .gnu.hash: its header, the Bloom filter (two bits per export, by bits 0 to 5 and 26 to 31 of its hash, in the
        // word bits 6 and up select), each bucket's first .dynsym index (0 for none) and the chain of hashes, the last
        // of each bucket's with its low bit set.
        uint8_t* gnu_hash = shared->section_bytes[EXECUTABLE_SECTION_GNU_HASH];
        memset(gnu_hash, 0, totals->linker_bytes[EXECUTABLE_SECTION_GNU_HASH]);
        const uint32_t header[4] = {
            buckets,
            (uint32_t)(1 + totals->undefined),
            (uint32_t)totals->bloom_words,
            26,
        };
        memcpy(gnu_hash, header, sizeof(header));
        uint8_t* bloom = gnu_hash + sizeof(header);
        uint8_t* bucket_bytes = bloom + 8 * totals->bloom_words;
        uint8_t* chain = bucket_bytes + 4 * (uint64_t)buckets;
        const ExecutableSectionLayout* tdata = &layout->sections[EXECUTABLE_SECTION_TDATA];
        uint64_t name_offset = totals->export_names;
        for (uint32_t k = 0; k < count; k += 1)
        {
            const uint32_t e = order[k];
            const SymbolId symbol = dynamic->exports[e];
            const uint32_t hash = hashes[e];
            const uint32_t b = hash % buckets;
            const uint32_t index = (uint32_t)(1 + totals->undefined + k);
            const NameId name = symbols->name[symbol];
            const uint64_t length = link->names.length[name];
            memcpy(dynstr + name_offset, link->names.bytes[name], length);
            dynstr[name_offset + length] = 0;
            // Defined in its atom's section at its address, or for a TLS symbol at its offset in the TLS segment.
            const AtomId atom = symbols->atom[symbol];
            const uint8_t section = atom != 0 ? executable_class_section(link->atoms.class[atom], true) : EXECUTABLE_SECTION_COUNT;
            const bool tls = executable_is_tls(link, symbol);
            const uint64_t address = atom != 0 ? link->atoms.address[atom] + symbols->value[symbol] : (uint64_t)(int64_t)(int32_t)symbols->reference[symbol];
            dynsym[index] = (Elf64_Sym){
                .st_name = (uint32_t)name_offset,
                .st_info = (uint8_t)(symbols->flags[symbol] & (BUSTER_LINK_SYMBOL_BINDING | BUSTER_LINK_SYMBOL_TYPE)),
                .st_other = (uint8_t)(symbols->flags[symbol] >> 6),
                .st_shndx = (uint16_t)(section != EXECUTABLE_SECTION_COUNT ? layout->section_index[section] : SHN_ABS),
                .st_value = tls ? address - tdata->address : layout->base + address,
                .st_size = symbols->size[symbol],
            };
            if (versym != 0)
            {
                versym[index] = VER_NDX_GLOBAL;
            }
            symbols->dynamic_index[symbol] = index;
            uint64_t word = 0;
            memcpy(&word, bloom + 8 * ((hash / 64) % totals->bloom_words), sizeof(word));
            word |= 1ull << (hash % 64) | 1ull << ((hash >> 26) % 64);
            memcpy(bloom + 8 * ((hash / 64) % totals->bloom_words), &word, sizeof(word));
            const bool first = k == 0 || hashes[order[k - 1]] % buckets != b;
            const bool last = k + 1 == count || hashes[order[k + 1]] % buckets != b;
            const uint32_t chained = last ? hash | 1 : hash & ~1u;
            memcpy(chain + 4 * (uint64_t)k, &chained, sizeof(chained));
            if (first)
            {
                memcpy(bucket_bytes + 4 * (uint64_t)b, &index, sizeof(index));
            }
            name_offset += length + 1;
        }

        // The needed libraries' names, then the needed versions' names, each with its NUL, and .gnu.version_r as lld lays
        // it out: an entry per library with a needed version, in ExecutableDynamic's order, then the auxiliary entries,
        // one per needed version, in ExecutableDynamic's order, which has each library's together.
        for (uint32_t l = 0; l < BUSTER_EXECUTABLE_LIBRARIES; l += 1)
        {
            if (totals->needed[l])
            {
                memcpy(dynstr + totals->needed_names[l], dynamic->needed[l].pointer, dynamic->needed[l].length);
                dynstr[totals->needed_names[l] + dynamic->needed[l].length] = 0;
            }
        }
        uint8_t* verneed = shared->section_bytes[EXECUTABLE_SECTION_VERNEED];
        const uint64_t files = totals->verneed_files;
        uint64_t file = 0;
        uint64_t first_auxiliary = 0;
        for (uint32_t l = 0; l < BUSTER_EXECUTABLE_LIBRARIES; l += 1)
        {
            if (totals->library_versions[l] != 0)
            {
                // vn_aux is from this entry to its library's first auxiliary entry, after every library's entry.
                const Elf64_Verneed needed = {
                    .vn_version = VER_NEED_CURRENT,
                    .vn_cnt = (uint16_t)totals->library_versions[l],
                    .vn_file = (uint32_t)totals->needed_names[l],
                    .vn_aux = (uint32_t)((files - file) * sizeof(Elf64_Verneed) + first_auxiliary * sizeof(Elf64_Vernaux)),
                    .vn_next = file + 1 < files ? sizeof(Elf64_Verneed) : 0,
                };
                memcpy(verneed + file * sizeof(Elf64_Verneed), &needed, sizeof(needed));
                file += 1;
                first_auxiliary += totals->library_versions[l];
            }
        }
        uint64_t version_name = totals->version_names;
        uint32_t auxiliary = 0;
        uint64_t library_auxiliaries[BUSTER_EXECUTABLE_LIBRARIES] = { 0 };
        for (uint32_t v = 0; v < dynamic->version_count; v += 1)
        {
            const ExecutableVersion* version = &dynamic->versions[v];
            const uint16_t number = shared->version_numbers[version->index];
            // The first time an index is given.
            const bool needed = number != 0 && number == 2 + auxiliary;
            if (needed)
            {
                memcpy(dynstr + version_name, version->name.pointer, version->name.length);
                dynstr[version_name + version->name.length] = 0;
                library_auxiliaries[version->library] += 1;
                const Elf64_Vernaux entry = {
                    .vna_hash = version->hash,
                    .vna_flags = 0,
                    .vna_other = number,
                    .vna_name = (uint32_t)version_name,
                    .vna_next = library_auxiliaries[version->library] < totals->library_versions[version->library] ? sizeof(Elf64_Vernaux) : 0,
                };
                memcpy(verneed + files * sizeof(Elf64_Verneed) + auxiliary * sizeof(Elf64_Vernaux), &entry, sizeof(entry));
                version_name += version->name.length + 1;
                auxiliary += 1;
            }
        }

        // .interp (a dynamic PIE's) and the ABI note, copied, and .dynamic.
        uint8_t* interp = shared->section_bytes[EXECUTABLE_SECTION_INTERP];
        if (interp != 0)
        {
            memcpy(interp, dynamic->interpreter.pointer, dynamic->interpreter.length);
            interp[dynamic->interpreter.length] = 0;
        }
        if (dynamic->note.length != 0)
        {
            memcpy(shared->section_bytes[EXECUTABLE_SECTION_ABI_NOTE], dynamic->note.pointer, dynamic->note.length);
        }
        const uint64_t init = dynamic->init != 0 ? layout->base + (uint64_t)(int64_t)(int32_t)symbols->reference[dynamic->init] : 0;
        const uint64_t fini = dynamic->fini != 0 ? layout->base + (uint64_t)(int64_t)(int32_t)symbols->reference[dynamic->fini] : 0;
        executable_dynamic_entries(layout, totals, init, fini, (Elf64_Dyn*)shared->section_bytes[EXECUTABLE_SECTION_DYNAMIC]);
    }
    return result;
}

// Allocates what the lanes share, from lane 0's scratch arena: 0 if it doesn't fit.
static ExecutableShared* executable_allocate(Arena* arena, const Link* link, uint32_t lanes, const ExecutableOptions* options)
{
    const ExecutableDynamic* dynamic = options->dynamic;
    ExecutableShared* shared = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableShared, 1);
    bool fits = shared != 0;
    if (fits)
    {
        *shared = (ExecutableShared){ 0 };
        shared->words_per_lane = link->symbols.count / 64 + 1;
        shared->counts = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableCounts, lanes);
        shared->errors = arena_push_array(arena, (uint64_t)lanes * EXECUTABLE_PHASES, sizeof(ExecutableLaneError), BUSTER_LANE_CACHE_LINE_BYTES);
        shared->got_marks = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, lanes * shared->words_per_lane);
        shared->got_counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, lanes);
        shared->addend_magnitudes = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, lanes);
        shared->absolute_bounds = BUSTER_ARENA_PUSH_ARRAY(arena, int64_t, 2 * (uint64_t)lanes);
        // The atoms' pieces and the linker's: the headers, its sections', .shstrtab and the section headers.
        shared->pieces = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutablePiece, (uint64_t)link->atoms.count + EXECUTABLE_SECTION_COUNT + 3);
        shared->exact = options->exact;
        shared->runtime = options->runtime != 0;
        shared->crt = dynamic != 0 || shared->runtime;
        shared->imports = dynamic != 0 && !shared->runtime;
        shared->relax = dynamic != 0 && shared->runtime;
        shared->relaxed = shared->runtime && options->runtime->tls_get_addr < link->symbols.count ? options->runtime->tls_get_addr : 0;
        // The filters: a PIE's of the symbols outside the image, then the static runtime's IFUNCs'.
        shared->ifunc_filter = dynamic != 0 ? BUSTER_EXECUTABLE_FILTER_WORDS : 0;
        shared->filter_words = shared->ifunc_filter + (shared->runtime ? 2 * BUSTER_EXECUTABLE_FILTER_WORDS : 0);
        fits = shared->counts != 0 && shared->errors != 0 && shared->got_marks != 0 && shared->got_counts != 0 && shared->addend_magnitudes != 0 &&
               shared->absolute_bounds != 0 && shared->pieces != 0;
    }
    if (fits && (dynamic != 0 || shared->runtime))
    {
        shared->filters = arena_push_array(arena, (uint64_t)lanes * shared->filter_words, sizeof(uint32_t), 64);
        shared->dynamic_counts = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableDynamicCounts, lanes);
        shared->stub_marks = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, lanes * shared->words_per_lane);
        fits = shared->filters != 0 && shared->dynamic_counts != 0 && shared->stub_marks != 0;
    }
    if (fits && dynamic != 0)
    {
        // The versions' indices, below the largest one's.
        uint32_t limit = VER_NDX_GLOBAL + 1;
        for (uint32_t v = 0; v < dynamic->version_count; v += 1)
        {
            limit = dynamic->versions[v].index >= limit ? dynamic->versions[v].index + 1u : limit;
        }
        shared->version_limit = limit;
        shared->version_words = limit / 64 + 1;
        shared->relr = dynamic->relr;
        shared->dynamic_bits = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, shared->words_per_lane);
        shared->relative_bits = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, shared->words_per_lane);
        shared->relr_words = shared->relr ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, link->atoms.count) : 0;
        shared->versions_used = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, lanes * shared->version_words);
        shared->version_numbers = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, limit);
        fits = shared->dynamic_bits != 0 && shared->relative_bits != 0 && (!shared->relr || shared->relr_words != 0) && shared->versions_used != 0 &&
               shared->version_numbers != 0;
    }
    if (fits && shared->runtime)
    {
        shared->ifunc_bits = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, shared->words_per_lane);
        shared->relaxable_bits = shared->relax ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, shared->words_per_lane) : 0;
        shared->relax_marks = shared->relax ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, lanes * shared->words_per_lane) : 0;
        fits = shared->ifunc_bits != 0 && (!shared->relax || (shared->relaxable_bits != 0 && shared->relax_marks != 0));
    }
    if (fits)
    {
        memset(shared->errors, 0, (uint64_t)lanes * EXECUTABLE_PHASES * sizeof(ExecutableLaneError));
    }
    return fits ? shared : 0;
}

// Lane 0's part once the layout is known: the arrays sized by it, the linker's sections' bytes, and the linker's
// pieces. Returns false if the arena can't hold them.
static bool executable_prepare(Arena* arena, ExecutableShared* shared, const ExecutableLayout* layout, uint32_t lanes)
{
    const uint64_t parts = BUSTER_LANE_PARTS_PER_LANE * (uint64_t)lanes;
    shared->got = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, 8 * (layout->got_slots + 1));
    shared->boundaries = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, layout->chunk_count + 1);
    shared->hashes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_EXECUTABLE_BUILD_ID_BYTES * layout->chunk_count);
    shared->part_boundaries = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, parts + 1);
    shared->orders = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, parts * lanes);
    bool result = shared->got != 0 && shared->boundaries != 0 && shared->hashes != 0 && shared->part_boundaries != 0 && shared->orders != 0;
    // The sections of the linker's own that their phases fill, a dynamic PIE's, a static executable's with the C runtime
    // and .eh_frame_hdr: their first entries here.
    for (uint32_t section = 0; result && section < EXECUTABLE_SECTION_COUNT; section += 1)
    {
        const bool own = section != EXECUTABLE_SECTION_BUILD_ID && section != EXECUTABLE_SECTION_GOT && executable_linker_before[section] != BUSTER_LINK_CLASS_COUNT &&
                         (layout->crt || section == EXECUTABLE_SECTION_EH_FRAME_HDR);
        const uint64_t size = layout->sections[section].size;
        shared->section_bytes[section] = own && size != 0 ? arena_push_array(arena, size, 1, 8) : 0;
        result = !own || size == 0 || shared->section_bytes[section] != 0;
    }
    if (result && layout->dynamic)
    {
        memset(shared->section_bytes[EXECUTABLE_SECTION_DYNSYM], 0, sizeof(Elf64_Sym));
        shared->section_bytes[EXECUTABLE_SECTION_DYNSTR][0] = 0;
        if (shared->section_bytes[EXECUTABLE_SECTION_VERSYM] != 0)
        {
            memset(shared->section_bytes[EXECUTABLE_SECTION_VERSYM], 0, sizeof(Elf64_Half));
        }
    }
    if (result)
    {
        memset(shared->got, 0, 8 * (layout->got_slots + 1));
        shared->boundaries[0] = 0;
        shared->boundaries[layout->chunk_count] = layout->file_size;
        ExecutablePiece* pieces = shared->pieces;
        shared->section_bytes[EXECUTABLE_SECTION_BUILD_ID] = shared->headers.note;
        shared->section_bytes[EXECUTABLE_SECTION_GOT] = shared->got;
        pieces[0] = (ExecutablePiece){
            .offset = 0,
            .size = layout->header_bytes,
            .bytes = shared->headers.headers,
        };
        for (uint32_t section = 0; section < EXECUTABLE_SECTION_COUNT; section += 1)
        {
            const ExecutableSectionLayout* placed = &layout->sections[section];
            if (layout->section_piece[section] != 0)
            {
                pieces[layout->section_piece[section]] = (ExecutablePiece){
                    .offset = placed->offset,
                    .size = placed->size,
                    .bytes = shared->section_bytes[section],
                };
            }
        }
        pieces[layout->piece_count - 2] = (ExecutablePiece){
            .offset = layout->shstrtab_offset,
            .size = layout->shstrtab_size,
            .bytes = (uint8_t*)shared->headers.shstrtab,
        };
        pieces[layout->piece_count - 1] = (ExecutablePiece){
            .offset = layout->section_headers_offset,
            .size = layout->section_header_count * sizeof(Elf64_Shdr),
            .bytes = (uint8_t*)shared->headers.section_headers,
        };
    }
    return result;
}

// Records a failed write of the output in a lane's error.
static void executable_lane_fail_write(ExecutableLaneError* error, int system_error)
{
    if (error->error == EXECUTABLE_ERROR_NONE)
    {
        executable_lane_fail(error, EXECUTABLE_ERROR_FILE, 0, 0, 0);
        error->system_error = system_error;
    }
}

// Patches and hashes chunk `chunk`, and writes it while it is in L2, unless `file` is -1.
static void executable_chunk(Link* link, ExecutableShared* shared, const ExecutableLayout* layout, const ExecutablePatch* patch, uint64_t chunk, int file,
                             ExecutableLaneError* error)
{
    const uint64_t begin = shared->boundaries[chunk];
    const uint64_t end = shared->boundaries[chunk + 1];
    const FixupId failed = executable_patch_chunk(link, patch, shared->pieces, layout->piece_count, begin, end);
    if (failed != 0)
    {
        // The smallest fixup that failed, which doesn't depend on the parts' order.
        error->fixup = error->error == EXECUTABLE_ERROR_FIXUP && error->fixup < failed ? error->fixup : failed;
        error->error = EXECUTABLE_ERROR_FIXUP;
    }
    Scratch scratch = scratch_begin(0);
    const uint64_t capacity = executable_run_capacity(shared->pieces, layout->piece_count, begin, end);
    struct iovec* runs = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, struct iovec, capacity);
    if (runs == 0)
    {
        executable_lane_fail(error, EXECUTABLE_ERROR_MEMORY, 0, 0, 0);
    }
    else
    {
        const uint64_t count = executable_chunk_runs(layout, shared->pieces, layout->piece_count, begin, end, runs);
        ExecutableHash hash;
        executable_hash_begin(&hash);
        for (uint64_t run = 0; run < count; run += 1)
        {
            executable_hash_bytes(&hash, runs[run].iov_base, runs[run].iov_len);
        }
        executable_hash_end(&hash, shared->hashes + BUSTER_EXECUTABLE_BUILD_ID_BYTES * chunk);
        const int written = file >= 0 && error->error == EXECUTABLE_ERROR_NONE ? executable_write_all(file, runs, count, begin, false) : 0;
        if (written != 0)
        {
            executable_lane_fail_write(error, written);
        }
    }
    scratch_end(scratch);
}

// Writes the whole file in order, a chunk at a time, for an output that is written in order. Returns 0, or the errno of
// the write that failed.
static int executable_write_in_order(int file, const ExecutableLayout* layout, const ExecutableShared* shared)
{
    int result = 0;
    for (uint64_t chunk = 0; result == 0 && chunk < layout->chunk_count; chunk += 1)
    {
        const uint64_t begin = shared->boundaries[chunk];
        const uint64_t end = shared->boundaries[chunk + 1];
        Scratch scratch = scratch_begin(0);
        const uint64_t capacity = executable_run_capacity(shared->pieces, layout->piece_count, begin, end);
        struct iovec* runs = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, struct iovec, capacity);
        result = runs == 0 ? ENOMEM : executable_write_all(file, runs, executable_chunk_runs(layout, shared->pieces, layout->piece_count, begin, end, runs), begin, true);
        scratch_end(scratch);
    }
    return result;
}

// Patches, hashes and writes the chunks of the parts this lane takes.
static void executable_chunks(Link* link, ExecutableShared* shared, const ExecutableLayout* layout, const ExecutablePatch* patch, int file, uint32_t lane, uint32_t lanes)
{
    uint32_t* order = shared->orders + lane * BUSTER_LANE_PARTS_PER_LANE * (uint64_t)lanes;
    const uint64_t parts = lane_parts_by_cost(shared->boundaries + 1, layout->chunk_count, shared->part_boundaries, order);
    for (uint64_t taken = lane_take(parts); taken < parts; taken = lane_take(parts))
    {
        const uint32_t part = order[taken];
        for (uint64_t chunk = shared->part_boundaries[part]; chunk < shared->part_boundaries[part + 1]; chunk += 1)
        {
            executable_chunk(link, shared, layout, patch, chunk, file, executable_error(shared, EXECUTABLE_PHASE_CHUNKS, lane, lanes));
        }
    }
}

// This lane's part of .eh_frame_hdr's table (see "Unwind tables"): the entries of its share of the .eh_frame atoms'
// FDEs, which executable_count_frames counted, from the lanes' before it: an atom the vector path read by its fixups
// (executable_eh_frame_entries), one the walk read by the walk; an atom with an entry that doesn't fit fails the link.
// And lane 0 the header's first 12 bytes.
static void executable_eh_frame_table(const Link* link, ExecutableShared* shared, const ExecutableLayout* layout, const ExecutableFrames* frames, uint32_t lane,
                                      uint32_t lanes, ExecutableLaneError* error)
{
    const ExecutableSectionLayout* header = &layout->sections[EXECUTABLE_SECTION_EH_FRAME_HDR];
    uint8_t* bytes = shared->section_bytes[EXECUTABLE_SECTION_EH_FRAME_HDR];
    int32_t* table = (int32_t*)(bytes + BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES);
    uint64_t index = 0;
    uint64_t fdes = 0;
    for (uint32_t other = 0; other < lanes; other += 1)
    {
        index += other < lane ? shared->counts[other].fdes : 0;
        fdes += shared->counts[other].fdes;
    }
    for (uint32_t f = 0; f < frames->count; f += 1)
    {
        const AtomId atom = frames->atoms[f];
        const uint32_t count = frames->fdes[f] & ~BUSTER_EXECUTABLE_FRAMES_WALKED;
        const uint64_t address = link->atoms.address[atom];
        const bool fits = (frames->fdes[f] & BUSTER_EXECUTABLE_FRAMES_WALKED) != 0 ? executable_eh_frame_walk(link, atom, table + 2 * index, header->address, address) == count
                                                                                    : executable_eh_frame_entries(link, atom, table + 2 * index, header->address, address);
        if (!fits)
        {
            executable_lane_fail(error, EXECUTABLE_ERROR_UNWIND, atom, 0, 0);
        }
        index += count;
    }
    if (lane == 0)
    {
        const int32_t pointer = (int32_t)(layout->sections[EXECUTABLE_SECTION_EH_FRAME].address - (header->address + 4));
        const uint32_t count = (uint32_t)fdes;
        bytes[0] = 1;
        bytes[1] = BUSTER_EXECUTABLE_EH_PE_PCREL_SDATA4;
        bytes[2] = BUSTER_EXECUTABLE_EH_PE_UDATA4;
        bytes[3] = BUSTER_EXECUTABLE_EH_PE_DATAREL_SDATA4;
        memcpy(bytes + 4, &pointer, sizeof(pointer));
        memcpy(bytes + 8, &count, sizeof(count));
    }
}

// Lane 0: whether .eh_frame_hdr's `fdes` entries are sorted by initial location (as signed values, the table's sdata4),
// 16 at a time, the even words of two vectors; if they aren't, or with `exact`, sorts them, stably, by initial location
// (an LSD radix sort, 8 bits a pass). Their order is .eh_frame's, so equal initial locations stay in address order
// either way. Returns false if the arena has no room for the sort.
static bool executable_eh_frame_sort(uint8_t* bytes, uint64_t fdes, bool exact, Arena* arena, bool* in_order)
{
    int32_t* table = (int32_t*)(bytes + BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES);
    const __m512i evens = _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
    bool sorted = true;
    // The last group's initial locations, whose last lane the next group's first must be at or after.
    __m512i last = _mm512_set1_epi32(INT32_MIN);
    for (uint64_t i = 0; sorted && i < fdes; i += 16)
    {
        const uint32_t n = fdes - i < 16 ? (uint32_t)(fdes - i) : 16;
        const uint32_t words = 2 * n;
        const __m512i first = _mm512_maskz_loadu_epi32((__mmask16)_bzhi_u32(0xffff, words), table + 2 * i);
        const __m512i second = _mm512_maskz_loadu_epi32((__mmask16)_bzhi_u32(0xffff, words > 16 ? words - 16 : 0), table + 2 * i + 16);
        const __m512i initial = _mm512_permutex2var_epi32(first, evens, second);
        const __m512i before = _mm512_alignr_epi32(initial, last, 15);
        sorted = _mm512_mask_cmpgt_epi32_mask((__mmask16)_bzhi_u32(0xffff, n), before, initial) == 0;
        last = initial;
    }
    *in_order = sorted;
    bool result = true;
    if (!sorted || exact)
    {
        uint64_t* entries = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, 2 * fdes);
        result = entries != 0;
        uint64_t* from = entries;
        uint64_t* to = entries != 0 ? entries + fdes : 0;
        if (result)
        {
            memcpy(from, table, 8 * fdes);
        }
        // Each entry as 64 bits: its initial location in the low 32, which a pass's digit comes from, with the sign bit
        // flipped so that unsigned order is the signed one.
        for (uint32_t pass = 0; result && pass < 4; pass += 1)
        {
            uint64_t starts[257] = { 0 };
            const uint32_t shift = 8 * pass;
            for (uint64_t e = 0; e < fdes; e += 1)
            {
                starts[((((uint32_t)from[e] ^ 0x80000000u) >> shift) & 0xff) + 1] += 1;
            }
            for (uint32_t digit = 0; digit < 256; digit += 1)
            {
                starts[digit + 1] += starts[digit];
            }
            for (uint64_t e = 0; e < fdes; e += 1)
            {
                const uint32_t digit = (((uint32_t)from[e] ^ 0x80000000u) >> shift) & 0xff;
                to[starts[digit]] = from[e];
                starts[digit] += 1;
            }
            uint64_t* swapped = from;
            from = to;
            to = swapped;
        }
        // An even number of passes: the entries are back where they were copied to first.
        if (result)
        {
            memcpy(table, from, 8 * fdes);
        }
    }
    return result;
}

static bool executable_write(Link* link, const ExecutableOptions* options, ExecutableResult* result)
{
    const uint32_t lane = lane_index();
    const uint32_t lanes = lane_count();
    const ExecutableDynamic* dynamic_options = options->dynamic;
    // A PIE (dynamic, or with the static runtime a static PIE), and the static runtime (a static executable with the C
    // runtime, or a static PIE): its IFUNCs, its TLS accesses' relaxation and the names its startup looks for.
    const bool dynamic = dynamic_options != 0;
    const bool runtime = options->runtime != 0;
    Scratch scratch = scratch_begin(0);
    ExecutableShared* allocated = 0;
    if (lane == 0)
    {
        *result = (ExecutableResult){ 0 };
        result->start_time = executable_now();
        allocated = executable_allocate(scratch.arena, link, lanes, options);
    }
    ExecutableShared* shared = (ExecutableShared*)lane_broadcast((uint64_t)allocated, 0);
    bool ok = shared != 0;

    // The static runtime's IFUNCs first, then a PIE's symbols (whose relaxable ones, in a static PIE, aren't IFUNCs): the
    // bitsets and the filters that the count's tests read. Then count the atoms and the GOT's targets (and a PIE's
    // fixups, and the IFUNCs' stubs), then reduce the GOT's marks (and classify a PIE's ABS64 fixups, and lane 0 numbers
    // a dynamic PIE's versions). Lane 0 checks the entry point first, so that a bad one is the error reported with any
    // number of lanes.
    if (ok && (dynamic || runtime))
    {
        shared->dynamic_counts[lane] = (ExecutableDynamicCounts){ 0 };
    }
    if (ok && runtime)
    {
        executable_mark_ifuncs(link, shared, lane);
    }
    if (ok && dynamic)
    {
        executable_mark_symbols(link, shared, lane);
    }
    if (dynamic || runtime)
    {
        lane_sync();
    }
    const ExecutableFilter filter = ok && dynamic ? executable_filter(shared, lanes, 0, false) : (ExecutableFilter){ 0 };
    const ExecutableFilter ifunc_filter = ok && runtime ? executable_filter(shared, lanes, shared->ifunc_filter, true) : (ExecutableFilter){ 0 };
    const SymbolId entry = options->entry;
    const AtomId entry_atom = entry != 0 && entry < link->symbols.count ? link->symbols.atom[entry] : 0;
    if (ok && lane == 0 && (entry_atom == 0 || link->atoms.class[entry_atom] != BUSTER_LINK_CLASS_TEXT))
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_COUNT, lane, lanes), EXECUTABLE_ERROR_ENTRY, 0, entry, 0);
    }
    ExecutableFrames frames = { 0 };
    if (ok)
    {
        executable_count(link, shared, lane, dynamic, &filter, &ifunc_filter, executable_error(shared, EXECUTABLE_PHASE_COUNT, lane, lanes));
        executable_count_frames(link, shared, lane, lanes, &frames, scratch.arena, executable_error(shared, EXECUTABLE_PHASE_COUNT, lane, lanes));
    }
    lane_sync();
    ok = ok && !executable_any_failed(shared, EXECUTABLE_PHASE_COUNT, lanes);
    if (ok)
    {
        executable_reduce_got(link, shared, lane, lanes);
    }
    if (ok && dynamic)
    {
        executable_classify(link, shared, &filter, lane);
    }
    if (ok && dynamic && lane == 0)
    {
        executable_number_versions(shared, dynamic_options, lanes);
    }
    lane_sync();
    if (lane == 0)
    {
        result->phase_times[EXECUTABLE_PHASE_COUNT] = executable_now();
    }

    // Lay out (every lane alike), place each lane's atoms, slots and stubs, and have lane 0 allocate what the layout
    // sizes. A lane without room for the layout fails in its error slot, so that the sync below stops every lane. Lane 0
    // first looks for the fixups a dynamic PIE can't relocate, when the counts say there are some.
    if (ok && dynamic && lane == 0)
    {
        executable_check_relocations(link, shared, lanes, executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes));
    }
    // A static executable's calls of __tls_get_addr must be its TLS accesses': one each, which their relaxation checks.
    uint64_t tls_accesses = 0;
    uint64_t relaxed_calls = 0;
    for (uint32_t other = 0; ok && runtime && other < lanes; other += 1)
    {
        tls_accesses += shared->dynamic_counts[other].tls_accesses;
        relaxed_calls += shared->dynamic_counts[other].relaxed_calls;
    }
    if (ok && runtime && lane == 0 && tls_accesses != relaxed_calls)
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes), EXECUTABLE_ERROR_UNSUPPORTED, 0, shared->relaxed, 0);
    }
    ExecutableLayout* layout = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ExecutableLayout, 1);
    if (ok && layout == 0)
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes), EXECUTABLE_ERROR_MEMORY, 0, 0, 0);
    }
    ok = ok && layout != 0;
    uint64_t got_slots = 0;
    uint64_t magnitude = 0;
    for (uint32_t other = 0; ok && other < lanes; other += 1)
    {
        got_slots += shared->got_counts[other];
        magnitude = shared->addend_magnitudes[other] > magnitude ? shared->addend_magnitudes[other] : magnitude;
    }
    ExecutableDynamicTotals totals = { 0 };
    if (ok && dynamic)
    {
        executable_dynamic_totals(link, shared, dynamic_options, lanes, &totals);
    }
    // A static executable's .rela.iplt and .iplt, with the C runtime (a static PIE's are in executable_dynamic_totals).
    for (uint32_t other = 0; ok && runtime && !dynamic && other < lanes; other += 1)
    {
        totals.stubs += shared->dynamic_counts[other].stubs;
        totals.irelatives += shared->dynamic_counts[other].irelatives;
    }
    if (runtime && !dynamic)
    {
        // An IRELATIVE relocation for each stub's slot, and for each IFUNC's GOT slot without one.
        totals.irelatives += totals.stubs;
        totals.linker_bytes[EXECUTABLE_SECTION_ABI_NOTE] = options->runtime->note.length;
        totals.linker_bytes[EXECUTABLE_SECTION_RELA_IPLT] = totals.irelatives * sizeof(Elf64_Rela);
        totals.linker_bytes[EXECUTABLE_SECTION_PLT_GOT] = totals.stubs * BUSTER_EXECUTABLE_STUB_BYTES;
    }
    // .eh_frame_hdr's size: an executable with .eh_frame atoms has one, with an entry per FDE.
    uint64_t fdes = 0;
    bool unwind = false;
    for (uint32_t other = 0; ok && other < lanes; other += 1)
    {
        fdes += shared->counts[other].fdes;
        unwind = unwind || shared->counts[other].bytes[executable_bucket(BUSTER_LINK_CLASS_EH_FRAME, 0)] != 0;
    }
    totals.linker_bytes[EXECUTABLE_SECTION_EH_FRAME_HDR] = unwind ? BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES + 8 * fdes : 0;
    const bool laid_out =
        ok && executable_lay_out(layout, shared->counts, lanes, got_slots + (runtime ? totals.stubs : 0), dynamic, dynamic || runtime, runtime, totals.linker_bytes);
    if (laid_out)
    {
        layout->stub_slot = runtime ? got_slots + 1 : 0;
    }
    if (ok && !laid_out)
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes), EXECUTABLE_ERROR_LAYOUT, 0, 0, 0);
    }
    if (laid_out)
    {
        executable_place(link, shared, layout, lane, executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes));
    }
    // The sort's memory goes back before executable_prepare takes what stays.
    const uint64_t sorted = scratch.arena->length;
    if (laid_out && lane == 0 && !executable_place_prioritized(link, layout, shared->pieces, scratch.arena))
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes), EXECUTABLE_ERROR_MEMORY, 0, 0, 0);
    }
    arena_pop_to(scratch.arena, sorted);
    if (laid_out && lane == 0 && !executable_prepare(scratch.arena, shared, layout, lanes))
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes), EXECUTABLE_ERROR_MEMORY, 0, 0, 0);
    }
    lane_sync();
    ok = ok && !executable_any_failed(shared, EXECUTABLE_PHASE_LAYOUT, lanes);
    if (lane == 0)
    {
        result->phase_times[EXECUTABLE_PHASE_LAYOUT] = executable_now();
    }

    // The symbols' references, the GOT, the stubs and the undefined dynamic symbols, the chunks' boundaries, and lane 0
    // the headers.
    if (ok)
    {
        executable_resolve(link, shared, layout, lane, executable_error(shared, EXECUTABLE_PHASE_SYMBOLS, lane, lanes));
    }
    if (ok && lane == 0)
    {
        executable_write_headers(&shared->headers, layout, link->atoms.address[entry_atom] + link->symbols.value[entry]);
    }
    lane_sync();
    ok = ok && !executable_any_failed(shared, EXECUTABLE_PHASE_SYMBOLS, lanes);
    if (lane == 0)
    {
        result->phase_times[EXECUTABLE_PHASE_SYMBOLS] = executable_now();
    }

    // .eh_frame_hdr's table, each lane its atoms' entries; a dynamic PIE's relocations, and lane 0 its other dynamic
    // sections.
    const bool indexed = ok && layout->sections[EXECUTABLE_SECTION_EH_FRAME_HDR].size != 0;
    if (indexed)
    {
        executable_eh_frame_table(link, shared, layout, &frames, lane, lanes, executable_error(shared, EXECUTABLE_PHASE_DYNAMIC, lane, lanes));
    }
    if (ok && dynamic)
    {
        executable_dynamic_relocations(link, shared, layout, &totals, &filter, lane);
        executable_relr_relocations(link, shared, layout, &filter, lane, lanes);
    }
    if (ok && dynamic && lane == 0 && !executable_dynamic_sections(scratch.arena, link, shared, layout, dynamic_options, &totals))
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_DYNAMIC, lane, lanes), EXECUTABLE_ERROR_MEMORY, 0, 0, 0);
    }
    // The static runtime's IRELATIVE relocations, a static executable's in .rela.iplt and a static PIE's last in
    // .rela.dyn; and lane 0 copies a static executable's ABI note (a PIE's is ExecutableDynamic.note).
    if (ok && runtime)
    {
        Elf64_Rela* rela_dyn = (Elf64_Rela*)shared->section_bytes[EXECUTABLE_SECTION_RELA_DYN];
        const uint64_t before = totals.got_relative + totals.got_symbolic + totals.abs64_relative + totals.abs64_symbolic;
        Elf64_Rela* irelative = dynamic ? (rela_dyn != 0 ? rela_dyn + before : 0) : (Elf64_Rela*)shared->section_bytes[EXECUTABLE_SECTION_RELA_IPLT];
        executable_irelative_relocations(link, shared, layout, irelative, lanes, lane);
    }
    if (ok && runtime && !dynamic && lane == 0 && options->runtime->note.length != 0)
    {
        memcpy(shared->section_bytes[EXECUTABLE_SECTION_ABI_NOTE], options->runtime->note.pointer, options->runtime->note.length);
    }
    if (dynamic || indexed || runtime)
    {
        lane_sync();
    }
    ok = ok && !executable_any_failed(shared, EXECUTABLE_PHASE_DYNAMIC, lanes);
    if (lane == 0)
    {
        result->phase_times[EXECUTABLE_PHASE_DYNAMIC] = executable_now();
    }

    // Lane 0 sorts .eh_frame_hdr's table if it must, then opens the output, once nothing can fail but a fixup's value or
    // the file.
    ExecutableFile* file = 0;
    bool opened = false;
    bool in_order = true;
    if (ok && lane == 0 && indexed && !executable_eh_frame_sort(shared->section_bytes[EXECUTABLE_SECTION_EH_FRAME_HDR], fdes, shared->exact, scratch.arena, &in_order))
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_OPEN, lane, lanes), EXECUTABLE_ERROR_MEMORY, 0, 0, 0);
        ok = false;
    }
    if (ok && lane == 0)
    {
        file = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ExecutableFile, 1);
        opened = file != 0 && executable_open(file, options->path, result);
        if (!opened)
        {
            executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_OPEN, lane, lanes), file == 0 ? EXECUTABLE_ERROR_MEMORY : EXECUTABLE_ERROR_FILE, 0, 0, 0);
        }
        shared->sequential = opened && file->write == EXECUTABLE_WRITE_SEQUENTIAL;
    }
    // Every lane writes its chunks to lane 0's descriptor, which is -1 if the link failed before or the output couldn't
    // be opened; but none does to an output written in order, which lane 0 writes at the end.
    const int descriptor = (int)(int64_t)lane_broadcast((uint64_t)(int64_t)(ok && opened ? file->descriptor : -1), 0);
    ok = descriptor >= 0;
    const bool sequential = ok && shared->sequential;
    if (lane == 0)
    {
        result->phase_times[EXECUTABLE_PHASE_OPEN] = executable_now();
    }

    // Patch, hash and write the chunks. Every S + A - P of CALL and PC32 fits in int32 when the image's span from the
    // lowest reference to its end, and from its start to the highest reference, do with the largest addend: S is in
    // the image, address 0 (a weak symbol defined nowhere, at minus the base) or, in a static executable, an absolute
    // symbol's value; P is in the image.
    int64_t lowest = ok ? -(int64_t)layout->base : 0;
    int64_t highest = ok ? (int64_t)layout->image_size : 0;
    for (uint32_t other = 0; ok && other < lanes; other += 1)
    {
        lowest = shared->absolute_bounds[2 * other] < lowest ? shared->absolute_bounds[2 * other] : lowest;
        highest = shared->absolute_bounds[2 * other + 1] > highest ? shared->absolute_bounds[2 * other + 1] : highest;
    }
    const bool proven = ok && highest + (int64_t)magnitude < (int64_t)BUSTER_EXECUTABLE_MAX_ADDRESS &&
                        (int64_t)layout->image_size + (int64_t)magnitude - lowest < (int64_t)BUSTER_EXECUTABLE_MAX_ADDRESS;
    const ExecutablePatch patch = {
        .offsets = link->fixups.offset,
        .targets = link->fixups.target,
        .addends = link->fixups.addend,
        .kinds = link->fixups.kind,
        .wide_addends = link->fixups.wide_addends,
        .reference = link->symbols.reference,
        .got_slots = link->symbols.got_slot,
        .sizes = link->symbols.size,
        .base = ok ? layout->base : 0,
        .got = ok ? layout->sections[EXECUTABLE_SECTION_GOT].address : 0,
        .relaxed = shared != 0 ? shared->relaxed : 0,
        .relaxable = shared != 0 && shared->relax ? shared->relaxable_bits : 0,
        .checked = !proven,
    };
    if (ok)
    {
        executable_chunks(link, shared, layout, &patch, sequential ? -1 : descriptor, lane, lanes);
    }
    lane_sync();
    const bool patched = ok && !executable_any_failed(shared, EXECUTABLE_PHASE_CHUNKS, lanes);
    if (lane == 0)
    {
        result->phase_times[EXECUTABLE_PHASE_CHUNKS] = executable_now();
    }

    // Lane 0: the lanes' first failure, which is from the one phase that failed (the lowest lane's, since lanes take
    // ranges in order, but the smallest fixup's, since parts are taken in any order); then the build ID (the tree's
    // root), written last, and the file closed.
    bool succeeded = false;
    if (lane == 0)
    {
        for (uint64_t slot = 0; shared != 0 && slot < (uint64_t)EXECUTABLE_PHASES * lanes; slot += 1)
        {
            const ExecutableLaneError* error = &shared->errors[slot];
            const bool first = result->error == EXECUTABLE_ERROR_NONE || (error->error == EXECUTABLE_ERROR_FIXUP && result->error == EXECUTABLE_ERROR_FIXUP && error->fixup < result->fixup);
            if (error->error == EXECUTABLE_ERROR_FILE)
            {
                executable_fail_file(result, "pwritev", error->system_error);
            }
            else if (error->error != EXECUTABLE_ERROR_NONE && first)
            {
                result->error = error->error;
                result->atom = error->atom;
                result->symbol = error->symbol;
                result->fixup = error->fixup;
            }
        }
        result->error = shared == 0 ? EXECUTABLE_ERROR_MEMORY : result->error;
        bool written = patched && result->error == EXECUTABLE_ERROR_NONE;
        if (written)
        {
            ExecutableHash hash;
            executable_hash_begin(&hash);
            executable_hash_bytes(&hash, shared->hashes, BUSTER_EXECUTABLE_BUILD_ID_BYTES * layout->chunk_count);
            uint8_t* build_id = shared->headers.note + sizeof(Elf64_Nhdr) + 4;
            executable_hash_end(&hash, build_id);
            memcpy(result->build_id, build_id, BUSTER_EXECUTABLE_BUILD_ID_BYTES);
            const struct iovec piece = {
                .iov_base = build_id,
                .iov_len = BUSTER_EXECUTABLE_BUILD_ID_BYTES,
            };
            // An output written in order gets every chunk now, the note's hash in place; another, the hash alone.
            const int error = sequential ? executable_write_in_order(file->descriptor, layout, shared)
                                         : executable_write_all(file->descriptor, &piece, 1, layout->sections[EXECUTABLE_SECTION_BUILD_ID].offset + sizeof(Elf64_Nhdr) + 4, false);
            written = error == 0;
            if (!written)
            {
                executable_fail_file(result, sequential ? "writev" : "pwritev", error);
            }
        }
        if (opened)
        {
            result->write = file->write;
            succeeded = executable_close(file, layout->file_size, written, result) && written;
        }
        if (ok && options->boundaries != 0)
        {
            const uint64_t count = layout->chunk_count + 1 < options->boundary_capacity ? layout->chunk_count + 1 : options->boundary_capacity;
            memcpy(options->boundaries, shared->boundaries, count * sizeof(uint64_t));
        }
        if (ok)
        {
            result->file_size = layout->file_size;
            result->image_size = layout->image_size;
            result->chunks = layout->chunk_count;
            result->got_slots = layout->got_slots;
            result->dynamic_symbols = totals.undefined + totals.exports;
            result->relocations = totals.got_relative + totals.got_symbolic + totals.abs64_relative + totals.abs64_symbolic + totals.irelatives;
            result->relative_relocations = totals.got_relative + totals.abs64_relative;
            result->relr_relocations = totals.relr_relocations;
            result->relr_words = totals.relr_words;
            result->stubs = totals.stubs;
            result->fdes = indexed ? fdes : 0;
            result->fdes_in_order = in_order;
        }
        result->phase_times[EXECUTABLE_PHASE_FINISH] = executable_now();
    }
    succeeded = lane_broadcast(succeeded, 0) != 0;
    scratch_end(scratch);
    return succeeded;
}
