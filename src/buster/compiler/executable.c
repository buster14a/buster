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
//     note, DT_INIT's and DT_FINI's functions and the definitions to export; and the IFUNCs it defines (a foreign
//     link's, #394) get the static runtime's stubs, numbered before its imports', and IRELATIVE relocations, last in
//     .rela.dyn, as lld's .iplt entries have them (ExecutableShared.ifuncs); and its general- and local-dynamic TLS
//     accesses (libgcc.a's generic-morestack.o's, which split-stack code joins: #515) relax without their calls of the
//     loader's __tls_get_addr (ExecutableDynamic.tls_get_addr), to local-exec for its own thread-locals and, for an
//     imported one, to initial-exec through its GOT slot, which a TPOFF64 relocation fills, as GNU ld and lld relax
//     them (executable_relax_tls), while a call of __tls_get_addr that no access makes reaches it through its stub or
//     its slot, as any import's does;
//   - a static PIE (step 6), from a link that binding joined to the static PIE's runtime (rcrt1.o, crtbeginS.o and
//     crtendS.o with the static executable's archives), with both: an ExecutableDynamic without an interpreter, a
//     library, versions or exports, and the static executable's ExecutableRuntime (dynamic.c's
//     dynamic_static_pie_inputs). It is a PIE without dynamic symbols, which libc.a's startup relocates (its
//     _dl_relocate_static_pie applies .relr.dyn and .rela.dyn, its IFUNCs' IRELATIVE relocations last), with the static
//     executable's IFUNC stubs and TLS relaxation; and it needs its loads and calls through the GOT of the image's
//     symbols relaxed, as every kind has them (executable_relax_got), since rcrt1.o's _start calls through a slot before
//     the startup has relocated it;
//   - a shared library (step 6), with preemptible exports, initial-exec TLS slots, a distinct two-word TLSGD GOT
//     pair per referenced symbol and one TLSLD module pair. The loader fills DTPMOD64 module IDs and, for preemptible
//     TLSGD targets, DTPOFF64 offsets; a local TLSGD offset and a TLSLD access's DTPOFF32 are module-relative.
// The linker defines __init_array_start, __init_array_end, __fini_array_start, __fini_array_end and
// _GLOBAL_OFFSET_TABLE_ for all five, and _DYNAMIC for the dynamic images.
//
// The image, at BUSTER_EXECUTABLE_BASE (or a PIE's at 0, which the loader places), is lld's: four PT_LOAD segments in
// lld's order, each from the next page at its file offset's place in a page. File offsets equal virtual offsets from
// the image base (4 KB-congruent), with zero-filled gaps between loads so consumers recover one mapping base:
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
// LOAD_GOT, GOTPCREL and GOTTPOFF that a fixup reads unrelaxed, and of a CALL to a PIE's dynamic symbol (its stub reads
// the slot), numbered from 1 in symbol order (slot 0 stays zero, so that got_slot 0 means none), then one per stub of
// the static runtime's: the target's address, its TLS offset for a TLS symbol, 0 for a dynamic symbol, which the loader
// fills, and for an IFUNC its stub's address, or 0 without one, which its IRELATIVE relocation fills. An IFUNC whose
// stub only calls reach has no slot of its own: its loads through the GOT read its stub's, as lld's read its .got.plt
// entry. Every executable relaxes its CALL_GOT and LOAD_GOT fixups with the addend -4 to the image's symbols but
// IFUNCs (executable_relaxes), as GNU ld and lld relax them in an executable, and their targets need no slot from
// them; and the static runtime's and a dynamic PIE's general- and local-dynamic TLS accesses, which call
// __tls_get_addr, whose consumed TLS calls need no slot or stub even when an input defines the function.
// On the direct path the code generator binds before it selects instructions, so the program's code reaches its own
// symbols directly: the relaxation takes the C runtime's loads and calls, and the generated programs'.
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
// too, so that one test of each target serves both, and whose hits that aren't calls make the stubs stand for their
// IFUNCs' addresses; the IFUNCs' slots that IRELATIVE relocations fill, and those whose loads through the GOT read their
// stubs' slots, from the bitsets (executable_ifunc_slots and executable_reduce_got); and the TLS accesses and the calls
// of __tls_get_addr, counted by compares of 64 kinds and 16 targets at a time. Every executable's loads and calls
// through the GOT that may relax are found by compares of 64 kinds and addends, marked in a bitset of candidates whose
// relaxable symbols (the RELATIVE ones but the IFUNCs: executable_mark_symbols's for a PIE, executable_mark_relaxable's
// for a static executable, 64 symbols at a time) the reduction leaves out of the GOT 64 at a time (executable_unrelaxed),
// and relaxed one at a time in the patch's path for the other kinds. A TLS access's relaxation goes one access at a time
// in the patch's path for the kinds other than CALL and PC32, and lane 0 places the prioritized array atoms by a radix
// sort: both are few (docs/object-writer-and-linker.md, "The static executable with libc", has their costs).
//
// The columns it writes are the linker's: atoms' address (an image offset, the address less the base) and symbols'
// reference, got_slot and, in a PIE, dynamic_index (a dynamic symbol's or an export's .dynsym index, 0 for the others).
// A symbol's reference is its image offset, or a TLS symbol's offset from the thread pointer (variant II: from the end
// of the TLS block, which is the TLS segment's memory size rounded up to its alignment, since the segment starts on a
// multiple of its alignment), or for a weak symbol defined nowhere the image offset of address 0, or 0 if the symbol is
// thread-local, as GNU ld resolves one; in a PIE, a dynamic symbol's is its stub's image offset, or address 0's; and
// with the C runtime, an IFUNC's with a stub is its stub's image offset (an IFUNC is one with a resolver: a weak one
// defined nowhere is address 0, as any other is), and an undefined static __tls_get_addr's 0, the relaxation
// consuming its calls; and an absolute symbol's is its value less the base (one farther than int32 from the base is
// refused). Every fixup's field is patched in place, in its atom's bytes.
//
// Patching and writing go a chunk at a time (BUSTER_EXECUTABLE_CHUNK_BYTES, about 256 KB, a quarter of Zen 5's L2), in
// chunks that never split a field or a TLS access's sequence (executable_fixup_extent): boundary k is at k chunks'
// bytes, unless an atom of at most a chunk holds it, which then ends there, or a larger atom's field or sequence does,
// which then ends there. The boundaries depend on the layout alone, not
// on the lanes. For each chunk, a lane patches the fields in it (executable_patch_pc32 an atom's whose fixups are all
// CALL and PC32, executable_patch_mixed another's, each fixup by its kind), hashes it while it is in L2 and writes it
// from there with pwritev. Every lane writes the chunks it patches: the writes take turns at the file's lock in the
// kernel, but each lane's overlaps the others' patching (docs/object-writer-and-linker.md, "Patching and writing", has
// what one lane writing them all costs instead). With ExecutableOptions.stage (link-foreign's, whose atoms' bytes are
// mapped from its input files), a lane copies the chunk into its scratch and patches, hashes and writes the copy, so that
// no page of an input is copied on a write to it (executable_stage_chunk). The build ID is a tree of the chunks' hashes:
// the hash of the chunks' hashes in chunk order, so that the lanes need nothing from each other to compute it and it
// doesn't depend on how many there are. Its note is hashed as zeros and written last.
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
// main.c includes this file after lane.c, x86_64_encoder.c and link.c, and its String8. executable_write takes
// BUSTER_LINKER_ENTRY rather than static: main.c's units emit it in DYNAMIC alone. This file keeps the shared API,
// hash, unwind reader and debug helpers; executable_impl.c keeps the writer's private implementation. DYNAMIC and
// the EXECUTABLE test owner include those bodies, as do standalone consumers and main.c's complete driver.
// The implementation's layout is:
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
#include "host_isa.h"

// ---------------------------------------------------------------------------------------------------------------------
// API

// Where the image starts: GNU ld's base for an executable that isn't a PIE. A dynamic PIE starts at 0, as GNU ld's and
// lld's do, and the loader places it.
#define BUSTER_EXECUTABLE_BASE 0x400000ull
#define BUSTER_EXECUTABLE_PAGE_BYTES 4096ull
#define BUSTER_EXECUTABLE_PAGE_LOG2 12
// The image's end, as an address: every image offset and PC-relative value fits in int32 below it.
#define BUSTER_EXECUTABLE_MAX_ADDRESS (1ull << 31)
// The image's largest end, as an address. A zero-fill atom may extend the image past the 32-bit reference reach above,
// since it needs address space but no bytes; its fixups, and every symbol a fixup names, are then checked against the
// 32-bit reach, as GNU ld and lld allow a large .bss in the small code model. The image still stays inside x86-64's
// 47-bit user address space.
#define BUSTER_EXECUTABLE_MAX_IMAGE_ADDRESS (1ull << 47)
// The largest atom, and the largest TLS block: every offset from the thread pointer, from minus the block's size up to
// 0, fits in int32. .tbss takes no address space, so the image's limit doesn't bound the block. A zero-fill .bss atom may
// be larger, up to the image's bound: its size needs no address reach of its own, only room after the image.
#define BUSTER_EXECUTABLE_MAX_ATOM_BYTES (1ull << 31)
#define BUSTER_EXECUTABLE_MAX_TLS_BYTES (1ull << 31)
#define BUSTER_EXECUTABLE_MAX_ZERO_FILL_BYTES (1ull << 47)
// A generous bound on the linker's own sections, which a link's atom counts don't include: if the atoms come this close
// to the 32-bit reach, GOT candidates keep slots for a symbol the linker sections may put past it.
#define BUSTER_EXECUTABLE_FAR_SLACK (64ull << 20)
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
// whose link has unwind tables (EH_FRAME atoms) has .eh_frame and .eh_frame_hdr, its index. The note sections hold the
// read-only atoms whose input section is a .note one (file-scope asm's, #1848), after the build ID's note: one per
// alignment, from EXECUTABLE_SECTION_NOTE's, a page's, down to a byte's, each named after its first atom's input
// section (executable_bound_sections); only a link with such atoms has any.
typedef enum ExecutableSection
{
    EXECUTABLE_SECTION_INTERP,
    EXECUTABLE_SECTION_ABI_NOTE,
    EXECUTABLE_SECTION_BUILD_ID,
    EXECUTABLE_SECTION_NOTE,
    EXECUTABLE_SECTION_DYNSYM = EXECUTABLE_SECTION_NOTE + BUSTER_EXECUTABLE_PAGE_LOG2 + 1,
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
    EXECUTABLE_SECTION_DEBUG_INFO,
    EXECUTABLE_SECTION_DEBUG_ABBREV,
    EXECUTABLE_SECTION_DEBUG_LINE,
    EXECUTABLE_SECTION_DEBUG_ADDR,
    EXECUTABLE_SECTION_DEBUG_RNGLISTS,
    // The other DWARF sections a foreign object may have (ExecutableDebugSections), after debug.c's five.
    EXECUTABLE_SECTION_DEBUG_STR_OFFSETS,
    EXECUTABLE_SECTION_DEBUG_STR,
    EXECUTABLE_SECTION_DEBUG_LINE_STR,
    EXECUTABLE_SECTION_DEBUG_LOCLISTS,
    EXECUTABLE_SECTION_DEBUG_ARANGES,
    EXECUTABLE_SECTION_DEBUG_RANGES,
    EXECUTABLE_SECTION_DEBUG_LOC,
    EXECUTABLE_SECTION_DEBUG_FRAME,
    EXECUTABLE_SECTION_DEBUG_MACRO,
    EXECUTABLE_SECTION_DEBUG_MACINFO,
    EXECUTABLE_SECTION_DEBUG_NAMES,
    EXECUTABLE_SECTION_DEBUG_TYPES,
    EXECUTABLE_SECTION_DEBUG_PUBNAMES,
    EXECUTABLE_SECTION_DEBUG_PUBTYPES,
    EXECUTABLE_SECTION_DEBUG_GNU_PUBNAMES,
    EXECUTABLE_SECTION_DEBUG_GNU_PUBTYPES,
    EXECUTABLE_SECTION_SYMTAB,
    EXECUTABLE_SECTION_STRTAB,
    EXECUTABLE_SECTION_COUNT,
} ExecutableSection;

#include "native_debug.h"

#define BUSTER_EXECUTABLE_DEBUG_SECTIONS BUSTER_NATIVE_DEBUG_SECTIONS
// The DWARF sections, debug.c's five and the others, EXECUTABLE_SECTION_DEBUG_INFO onwards in that order: an
// ExecutableDebugSections's kinds.
#define BUSTER_EXECUTABLE_DEBUG_KINDS (EXECUTABLE_SECTION_SYMTAB - EXECUTABLE_SECTION_DEBUG_INFO)

// DWARF 5 units emitted by debug.c (info, abbrev, line, addr, rnglists in that order): each section's slice holds the
// units' contributions one after another, in unit order, each unit after the first written at its contributions' places
// in the slices (DebugOutput.bases), so that its header and DIE point at its own. `functions` and `line_addresses` are
// the units' functions one unit after another, unit k having unit_functions[k] of them (unit_count 0: one unit of all
// of them), and each line address is an offset in the whole .debug_line slice. The caller keeps all slices alive and
// unchanged for executable_write; no padding is required. The writer copies the bytes, then patches each function's
// address into its unit's .debug_addr contribution (unit k's starts at the earlier units' 8 + 8 * n bytes: function i
// of it at that plus 8 + 8 * i) and into .debug_line[line_addresses[i]]. Function ids describe ordinary TEXT
// definitions, not imports, IFUNCs or stubs. These addresses describe the definitions even if ELF interposition changes
// references to them. The producer must match the emitted function order and sizes to these definitions.
typedef struct ExecutableDebug ExecutableDebug;
struct ExecutableDebug
{
    String8 sections[BUSTER_EXECUTABLE_DEBUG_SECTIONS];
    NativeDebugExtra extra;
    const SymbolId* functions;
    const uint32_t* line_addresses;
    uint32_t function_count;
    const uint32_t* unit_functions;
    uint32_t unit_count;
};

// Debug sections a producer built whole, as a foreign link's are (foreign_debug.c, #395): each DWARF section's bytes by
// kind (section EXECUTABLE_SECTION_DEBUG_INFO + kind, empty for none), its contributions one after another, each
// section-relative reference among them resolved already, and the references whose values the layout decides, which
// the writer patches into those bytes in place, whatever their fields held: an address (ABS64, and ABS32 that must fit
// 32 bits) of a symbol's definition (its atom's place, not a stub's, an absolute symbol's value, or for a symbol
// without a definition its reference: address 0 for a weak one defined nowhere), plus the addend, as a debugger reads
// it from the file (a PIE's at its base of 0: no dynamic relocation reaches a section that isn't loaded); or a
// thread-local's offset in the TLS segment (DTPOFF32, which must fit 32 signed bits, and DTPOFF64) plus the addend. A
// reference whose target is 0 is one to discarded code (a section that section GC dropped): it gets lld's tombstone
// (executable_debug_tombstone). The references are columns, which a producer's vector paths fill a batch at a
// time: each one's field's offset in its section, its addend, its target, its section's kind (the field's section is
// EXECUTABLE_SECTION_DEBUG_INFO plus it) and its kind (EXECUTABLE_DEBUG_ABS64 and the others). Each section's
// alignment (`alignments`, 0 or 1 for none, else a power of two to 4096), its contributions' largest, places it in the
// file and is its sh_addralign, as GNU ld and lld align an output section. The bytes are the producer's, writable, and
// alive through executable_write with the columns; the writer takes this or ExecutableDebug, not both.
#define EXECUTABLE_DEBUG_ABS64 0
#define EXECUTABLE_DEBUG_ABS32 1
#define EXECUTABLE_DEBUG_DTPOFF32 2
#define EXECUTABLE_DEBUG_DTPOFF64 3

typedef struct ExecutableDebugSections ExecutableDebugSections;
struct ExecutableDebugSections
{
    String8 sections[BUSTER_EXECUTABLE_DEBUG_KINDS];
    uint64_t alignments[BUSTER_EXECUTABLE_DEBUG_KINDS];
    const uint32_t* offsets;
    const int64_t* addends;
    const SymbolId* targets;
    const uint8_t* section_kinds;
    const uint8_t* kinds;
    uint64_t relocation_count;
};

// Optional regular symbol table, independent of DWARF. IDs are strictly increasing and remain valid through writing.
// The writer retains declared names, sizes, bindings and visibility, emits locals first, and computes definitions
// from final atom placement (including TLS module offsets and IFUNC resolver addresses), never PLT/GOT references.
// With both attachments, this list supplies .symtab; without it, debug's function list retains its existing table.
typedef struct ExecutableSymbols ExecutableSymbols;
struct ExecutableSymbols
{
    const SymbolId* ids;
    uint32_t count;
};

typedef enum ExecutableError
{
    EXECUTABLE_ERROR_NONE,
    // Scratch memory ran out.
    EXECUTABLE_ERROR_MEMORY,
    // The image doesn't fit below BUSTER_EXECUTABLE_MAX_IMAGE_ADDRESS or the TLS block in BUSTER_EXECUTABLE_MAX_TLS_BYTES,
    // or an atom is larger than its class's limit (BUSTER_EXECUTABLE_MAX_ATOM_BYTES, or for .bss the image's) or aligned
    // past a page, an INIT_ARRAY or FINI_ARRAY atom isn't whole 8-byte entries aligned to at most 8, an atom of an
    // ordered class (EH_FRAME, and a dynamic PIE's INIT and FINI) doesn't land on its alignment where the one before it
    // ends, in a static executable an atom is of one of the C runtime's classes (INIT and FINI), which an executable
    // without libc has no section for, or a fixup names an atom or linker symbol past the 32-bit references' reach
    // (ExecutableResult.atom, .symbol and .cause).
    EXECUTABLE_ERROR_LAYOUT,
    // A symbol defined nowhere that isn't weak (ExecutableResult.symbol).
    EXECUTABLE_ERROR_UNDEFINED,
    // In a static executable, a symbol of a shared library; one the linker should define that it doesn't know; or with
    // the C runtime, an undefined __tls_get_addr called other than by as many general- and local-dynamic TLS accesses as there are
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

// The specific cause of an ExecutableError, where its class has several: a layout refusal names the atom's limit, an
// unwind refusal says whether the atom's size or one of its FDEs failed, and a relocation refusal says why the writer
// can't relocate the fixup. The driver prints it, so that a link's refusal names what to change.
typedef enum ExecutableCause
{
    EXECUTABLE_CAUSE_NONE,
    // EXECUTABLE_ERROR_MEMORY: the allocation that ran out, by phase (ExecutableResult.phase), or the shared state's
    // before any phase.
    EXECUTABLE_CAUSE_MEMORY_SHARED,
    EXECUTABLE_CAUSE_MEMORY_COUNT,
    EXECUTABLE_CAUSE_MEMORY_LAYOUT,
    EXECUTABLE_CAUSE_MEMORY_DYNAMIC,
    EXECUTABLE_CAUSE_MEMORY_OPEN,
    EXECUTABLE_CAUSE_MEMORY_CHUNKS,
    // EXECUTABLE_ERROR_LAYOUT, with an atom (ExecutableResult.atom): its size, its alignment, an init or fini array's
    // shape, a class this executable has no section for, an ordered class's alignment, or an atom whose start the
    // image's 32-bit references can't hold.
    EXECUTABLE_CAUSE_ATOM_TOO_LARGE,
    EXECUTABLE_CAUSE_ATOM_ALIGNMENT,
    EXECUTABLE_CAUSE_ATOM_ARRAY_ENTRY,
    EXECUTABLE_CAUSE_ATOM_CLASS,
    EXECUTABLE_CAUSE_ATOM_ORDER,
    // A fixup names an atom, or a linker-defined symbol, that starts past the 32-bit references' reach; or the atoms'
    // sizes overflow the image's address space.
    EXECUTABLE_CAUSE_ATOM_FAR,
    EXECUTABLE_CAUSE_ATOM_TOTAL_TOO_LARGE,
    EXECUTABLE_CAUSE_SYMBOL_FAR,
    EXECUTABLE_CAUSE_FAR_GOT,
    // EXECUTABLE_ERROR_LAYOUT, without an atom: the image's end, the TLS block, or the section-name table.
    EXECUTABLE_CAUSE_IMAGE_TOO_LARGE,
    EXECUTABLE_CAUSE_TLS_TOO_LARGE,
    EXECUTABLE_CAUSE_SECTION_NAMES_TOO_LARGE,
    // EXECUTABLE_ERROR_UNWIND: the atom is larger than the atom limit, its records are malformed, or an FDE's initial
    // location is outside the table's int32 range.
    EXECUTABLE_CAUSE_UNWIND_ATOM_TOO_LARGE,
    EXECUTABLE_CAUSE_UNWIND_ATOM_MALFORMED,
    EXECUTABLE_CAUSE_UNWIND_RANGE,
    // EXECUTABLE_ERROR_ENTRY: the symbol isn't defined, or it is defined in an atom that isn't TEXT.
    EXECUTABLE_CAUSE_ENTRY_UNDEFINED,
    EXECUTABLE_CAUSE_ENTRY_NOT_TEXT,
    // EXECUTABLE_ERROR_UNSUPPORTED: an unknown symbol version, debug information, a symbol table, undefined __tls_get_addr
    // called outside a TLS access, a thread-local without its empty section, a string table over 4 GB, or a
    // __start_/__stop_-bound input section whose atoms one output section can't hold (ExecutableResult.symbol and
    // atom).
    EXECUTABLE_CAUSE_UNSUPPORTED_VERSION,
    EXECUTABLE_CAUSE_UNSUPPORTED_DEBUG,
    EXECUTABLE_CAUSE_UNSUPPORTED_DEBUG_SYMBOL,
    EXECUTABLE_CAUSE_UNSUPPORTED_SYMBOLS,
    EXECUTABLE_CAUSE_UNSUPPORTED_TLS_CALL,
    EXECUTABLE_CAUSE_UNSUPPORTED_TLS_SECTION,
    EXECUTABLE_CAUSE_UNSUPPORTED_STRING_TABLE,
    EXECUTABLE_CAUSE_UNSUPPORTED_BOUND,
    // EXECUTABLE_ERROR_RELOCATION: a text relocation, a 32-bit absolute value that needs a load address, an
    // unrelocatable use of a dynamic symbol, address 0, an absolute symbol, or a shared library's rule.
    EXECUTABLE_CAUSE_RELOCATION_TEXT,
    EXECUTABLE_CAUSE_RELOCATION_ABSOLUTE32,
    EXECUTABLE_CAUSE_RELOCATION_DYNAMIC,
    EXECUTABLE_CAUSE_RELOCATION_ZERO,
    EXECUTABLE_CAUSE_RELOCATION_ABSOLUTE,
    EXECUTABLE_CAUSE_RELOCATION_SHARED,
} ExecutableCause;

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
// link's symbols of source RUNTIME_SHARED or FOREIGN_SHARED, each at the version its column holds (an
// ExecutableVersion's index, or 0/VER_NDX_GLOBAL for none). An imported version above VER_NDX_GLOBAL must occur in
// the version metadata; the symbol pass refuses a missing definition before opening the output.
typedef struct ExecutableVersion ExecutableVersion;
struct ExecutableVersion
{
    String8 name;
    // The name's ELF hash: the library's vd_hash, which the executable's vna_hash repeats.
    uint32_t hash;
    // Its index among the libraries' version definitions, which runtime.c numbers apart (the loader's after
    // libc.so.6's).
    uint16_t index;
    // The library it is a version of: its place in ExecutableDynamic.libraries.
    uint32_t library;
};

typedef struct ExecutableLibrary ExecutableLibrary;
struct ExecutableLibrary
{
    String8 name;
    // The producer already selected this library, including an unversioned --as-needed dependency. Otherwise a
    // used version selects it. The runtime uses that latter rule for libc.so's as-needed loader.
    bool required;
};

typedef struct ExecutableDynamic ExecutableDynamic;
struct ExecutableDynamic
{
    String8 interpreter;
    // Bounded, borrowed arrays, immutable until executable_write returns. Library order is DT_NEEDED order;
    // versions are grouped in that order, with unique indices above VER_NDX_GLOBAL and below 0x8000. Empty arrays
    // may be null. The producer resolves --as-needed before publishing unversioned dependencies as required.
    const ExecutableLibrary* libraries;
    uint32_t library_count;
    const ExecutableVersion* versions;
    uint32_t version_count;
    String8 note;
    SymbolId init;
    SymbolId fini;
    const SymbolId* exports;
    uint32_t export_count;
    // Each export's version by its place in `exports` (an ExecutableVersion's index, or 0 or VER_NDX_GLOBAL for the
    // executable's own definition), or null for all VER_NDX_GLOBAL; and the copies among the exports (copy relocations,
    // a foreign producer's): each a definition in a BSS atom of the link, where the loader copies a library's data of the
    // same name before anything runs (R_X86_64_COPY, last in .rela.dyn), as GNU ld and lld write one. A copy is exported
    // at the version of the library's definition, and so are its aliases, other exports at the same address.
    const uint16_t* export_versions;
    const SymbolId* copies;
    uint32_t copy_count;
    // The library's index of GLIBC_ABI_DT_RELR, whose definition says that its loader takes DT_RELR, or 0: with it,
    // the executable needs the version.
    uint16_t relr_version;
    // Whether the executable's RELATIVE relocations at even addresses are RELR's: a dynamic PIE's with
    // GLIBC_ABI_DT_RELR, and a static PIE's when libc.a relocates it with DT_RELR (dynamic.c).
    bool relr;
    // A dynamic PIE's __tls_get_addr, or 0: each general- or local-dynamic TLS access relaxes with the call of it after
    // it, to local-exec for a thread-local of the image's and to initial-exec through a TPOFF64 slot for an import's
    // (executable_relax_tls), as GNU ld and lld relax them; any other call of it is an import's, through its stub or
    // its slot. With 0, the writer refuses the accesses (a producer that doesn't name it: link-foreign's). A shared
    // library keeps its calls.
    SymbolId tls_get_addr;
    // Whether the output is a shared library (-shared): one without an interpreter or an entry point
    // (ExecutableOptions.entry may be 0), whose global and weak definitions of default visibility are preemptible and
    // whose strong references to nothing of default visibility the loader binds (executable_relocation_class), with its
    // soname for DT_SONAME (empty for none).
    bool shared;
    String8 soname;
    // Optional runtime search path. A non-null pointer requests a tag even for an empty path; bytes are borrowed.
    String8 runpath;
    bool old_dtags;
};

// What a static executable that links the C runtime (crt1.o, libc.a's members and the rest of the driver's group, which
// binding joined) has that one without it doesn't, as plain data: a note to repeat (crt1.o's .note.ABI-tag). Its .init
// and .fini come from the crt objects' fragments, its IFUNCs from their symbols' type, and the names its linker defines
// from their symbols' source.
typedef struct ExecutableRuntime ExecutableRuntime;
struct ExecutableRuntime
{
    String8 note;
    // The link's __tls_get_addr, or 0. General- and local-dynamic accesses relax to local-exec without their calls.
    // If undefined (as bind.c leaves it), every reference must belong to an access; a definition keeps ordinary ones.
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
    // Optional caller-produced DWARF bytes, finalized with this layout and written outside the loaded segments: debug.c's
    // units, or debug sections a producer built whole (a foreign link's), not both.
    const ExecutableDebug* debug;
    const ExecutableDebugSections* debug_sections;
    const ExecutableSymbols* symbols;
    // For the tests: a dynamic PIE's passes by their exact twins, scalar, which must write the same file.
    bool exact;
    // For a link whose atoms' bytes are borrowed from files mapped privately (link-foreign's): each chunk is copied into
    // a lane's buffer, patched there, hashed and written from it, so that the atoms' bytes are only read. Patched in
    // place, each page of a mapped file would be copied on its first write. An output written in order (a FIFO) is
    // patched in place still.
    bool stage;
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
    // What the error is specifically (ExecutableCause), and the value and limit it compares, or the phase that failed
    // for EXECUTABLE_ERROR_MEMORY.
    ExecutableCause cause;
    ExecutablePhase phase;
    uint64_t value;
    uint64_t limit;
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
#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_DYNAMIC || BUSTER_UNIT == BUSTER_UNIT_EXECUTABLE || BUSTER_COMPLETE_DRIVER
BUSTER_LINKER_ENTRY __attribute__((unused)) bool executable_write(Link* link, const ExecutableOptions* options, ExecutableResult* result);
#else
__attribute__((unused)) bool executable_write(Link* link, const ExecutableOptions* options, ExecutableResult* result);
#endif

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

// The sections' names, types and flags, for .shstrtab and the section headers.
static const char* const executable_section_names[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_INTERP] = ".interp",
    [EXECUTABLE_SECTION_ABI_NOTE] = ".note.ABI-tag",
    [EXECUTABLE_SECTION_BUILD_ID] = ".note.gnu.build-id",
    // A placeholder: a note section takes its atoms' input section's name (ExecutableLayout.note_names).
    [EXECUTABLE_SECTION_NOTE ... EXECUTABLE_SECTION_DYNSYM - 1] = ".note",
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
    [EXECUTABLE_SECTION_DEBUG_INFO] = ".debug_info",
    [EXECUTABLE_SECTION_DEBUG_ABBREV] = ".debug_abbrev",
    [EXECUTABLE_SECTION_DEBUG_LINE] = ".debug_line",
    [EXECUTABLE_SECTION_DEBUG_ADDR] = ".debug_addr",
    [EXECUTABLE_SECTION_DEBUG_RNGLISTS] = ".debug_rnglists",
    [EXECUTABLE_SECTION_DEBUG_STR_OFFSETS] = ".debug_str_offsets",
    [EXECUTABLE_SECTION_DEBUG_STR] = ".debug_str",
    [EXECUTABLE_SECTION_DEBUG_LINE_STR] = ".debug_line_str",
    [EXECUTABLE_SECTION_DEBUG_LOCLISTS] = ".debug_loclists",
    [EXECUTABLE_SECTION_DEBUG_ARANGES] = ".debug_aranges",
    [EXECUTABLE_SECTION_DEBUG_RANGES] = ".debug_ranges",
    [EXECUTABLE_SECTION_DEBUG_LOC] = ".debug_loc",
    [EXECUTABLE_SECTION_DEBUG_FRAME] = ".debug_frame",
    [EXECUTABLE_SECTION_DEBUG_MACRO] = ".debug_macro",
    [EXECUTABLE_SECTION_DEBUG_MACINFO] = ".debug_macinfo",
    [EXECUTABLE_SECTION_DEBUG_NAMES] = ".debug_names",
    [EXECUTABLE_SECTION_DEBUG_TYPES] = ".debug_types",
    [EXECUTABLE_SECTION_DEBUG_PUBNAMES] = ".debug_pubnames",
    [EXECUTABLE_SECTION_DEBUG_PUBTYPES] = ".debug_pubtypes",
    [EXECUTABLE_SECTION_DEBUG_GNU_PUBNAMES] = ".debug_gnu_pubnames",
    [EXECUTABLE_SECTION_DEBUG_GNU_PUBTYPES] = ".debug_gnu_pubtypes",
    [EXECUTABLE_SECTION_SYMTAB] = ".symtab",
    [EXECUTABLE_SECTION_STRTAB] = ".strtab",
};

// .shstrtab's bytes: room for every section's name with its NUL (672 bytes with its first NUL and its own name, the 63
// names of executable_section_names; .iplt is shorter than .plt.got, which it replaces), which test-executable checks,
// and which executable_lay_out refuses a layout past. A note section's name is its input section's, which the rest
// holds: a link's notes are mostly of one or two alignments, whose names then have more than 300 bytes.
#define BUSTER_EXECUTABLE_SHSTRTAB_BYTES 1024

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

// lld's tombstone for a debug section's reference to discarded code or data (a field of EXECUTABLE_DEBUG_ABS64's kind or
// another in `section`): all ones in .debug_names, whose name index would read 0 as its first unit's offset (an ABS32's
// 32 bits, as lld truncates R_X86_64_32's), 1 in .debug_ranges and .debug_loc, whose lists 0 would end, and 0 elsewhere.
static uint64_t executable_debug_tombstone(uint32_t section, uint8_t kind)
{
    const uint64_t tombstone = section == EXECUTABLE_SECTION_DEBUG_NAMES                                          ? UINT64_MAX :
                               section == EXECUTABLE_SECTION_DEBUG_RANGES || section == EXECUTABLE_SECTION_DEBUG_LOC ? 1 :
                                                                                                                     0;
    return kind == EXECUTABLE_DEBUG_ABS32 ? (uint32_t)tombstone : tombstone;
}


#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_DYNAMIC || BUSTER_UNIT == BUSTER_UNIT_EXECUTABLE || BUSTER_COMPLETE_DRIVER
#include "buster/compiler/executable_impl.c"
#endif
