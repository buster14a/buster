// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The direct path's executables: a link (link.c) laid out, patched and written as an executable, in one call that every
// lane of a group makes (docs/object-writer-and-linker.md, "The direct path (no -c)", "Layout", "Patching and writing",
// "The first executables" and "The dynamic PIE"). It writes two kinds:
//   - a static executable at a fixed address, without libc, crt objects or dynamic sections (step 4 of the plan): the
//     program's _start makes its system calls;
//   - a dynamic PIE against glibc (step 5), from a link that binding (bind.c) joined to the C runtime, with what
//     ExecutableDynamic holds (dynamic.c fills it): the program interpreter, libc.so.6 and its versions, Scrt1.o's ABI
//     note, DT_INIT's and DT_FINI's functions and the definitions to export.
// The linker defines __init_array_start, __init_array_end, __fini_array_start, __fini_array_end and
// _GLOBAL_OFFSET_TABLE_ for both.
//
// The image, at BUSTER_EXECUTABLE_BASE (or a PIE's at 0, which the loader places), is lld's: four PT_LOAD segments in
// lld's order, each from the next page at its file offset's place in a page (4 KB-congruent, with contiguous file
// offsets and no file padding):
//   - R: the ELF header and program headers (which a program finds through AT_PHDR), a PIE's .interp and
//     .note.ABI-tag, .note.gnu.build-id, a PIE's .dynsym, .gnu.version, .gnu.version_r, .gnu.hash, .dynstr, .rela.dyn
//     and .relr.dyn, .rodata (RODATA atoms, then STRINGS atoms) and a PIE's .eh_frame;
//   - RX: .text, and a PIE's .init, .fini and .plt.got (its stubs);
//   - RW, RELRO: .tdata and .tbss (the TLS image, PT_TLS), .init_array, .fini_array, .data.rel.ro, a PIE's .dynamic,
//     and .got, its memory size running to its page's end so that PT_GNU_RELRO covers it all;
//   - RW: .data and .bss.
// Then the section headers' string table and the section headers, which no segment loads. Within a section, atoms are
// ordered by alignment, largest first, and by id within an alignment, so that each one's offset is a prefix sum of the
// sizes before it with no padding: a data atom's size is padded to a multiple of its alignment, and a function's to 16
// bytes with int3 (functions are at least 16-byte aligned). The atoms of INIT_ARRAY and FINI_ARRAY, whose order the
// program runs, keep id order: they must be whole 8-byte entries, aligned to at most 8. So do a PIE's .eh_frame, .init
// and .fini atoms, the fragments of one piece each, which must land on their alignment where the one before ends (on 4
// bytes for .eh_frame's records). Atoms aligned past a page aren't supported yet. The GOT has a slot for each target of
// CALL_GOT, LOAD_GOT, GOTPCREL and GOTTPOFF, and of a CALL to a PIE's dynamic symbol (its stub reads the slot),
// numbered from 1 in symbol order (slot 0 stays zero, so that got_slot 0 means none): the target's address, its TLS
// offset for a TLS symbol, or 0 for a dynamic symbol, which the loader fills. Nothing is relaxed: on the direct path
// the code generator binds before it selects instructions, so the program's code reaches its own symbols directly.
//
// A static executable needs no dynamic relocations. A PIE's come from each target's relocation class
// (executable_relocation_class): RELATIVE for the image's symbols, to whose addresses the loader adds the load
// address; SYMBOLIC for the dynamic symbols (an import, or a weak reference to nothing with default visibility, which a
// library may define), .dynsym's undefined entries; NONE for a TLS symbol's offset and a weak reference to nothing that
// isn't dynamic. A GOT slot gets a RELATIVE relocation, or GLOB_DAT (TPOFF64 for a TLS import); an ABS64 field of an atom
// the loader writes (TDATA, the init and fini arrays, DATA_RELRO and DATA), a RELATIVE relocation, or R_X86_64_64 with
// the fixup's addend. With RELR (libc.so.6 defining GLIBC_ABI_DT_RELR, which the executable then needs), the RELATIVE
// ones at even addresses are .relr.dyn's: each atom's encoded from its first, so that their words don't depend on the
// layout, and the GOT's a word of marks at a time. The others are .rela.dyn's, RELATIVE ones first. A CALL to a dynamic
// symbol goes to its stub in .plt.got (jmp [rip + slot], GNU ld's form). Refused (EXECUTABLE_ERROR_RELOCATION): an
// ABS64 that needs a relocation outside the atoms the loader writes (a text relocation), an ABS32 or ABS32S that needs
// one, and a fixup of another kind to a dynamic symbol, as lld refuses them; and a PC32 or PC64 to address 0 (a weak
// reference to nothing that isn't dynamic), which no field measured from where the image loads reaches. GNU ld refuses
// that PC32 in code and read-only data; a PC32 in writable data and a PC64 anywhere, it and lld write off by the load
// address.
//
// A PIE's passes are data-parallel and vectorized, each with an exact twin, scalar, that ExecutableOptions.exact selects
// and the tests compare it with: the symbols' classes as bitsets, 64 symbols at a time (executable_mark_symbols); the
// fixups' targets tested 16 at a time against a filter of the symbols outside the image in two registers
// (executable_filter_test), whose few hits the exact check tells apart; the atoms the loader writes found 64 at a time
// (executable_written_atoms), and a dense atom's RELR words from its fixup count alone (executable_atom_is_dense); and
// the GOT's slots by class and their RELR words from the bitsets (executable_slots_of_class, executable_got_relr).
//
// The columns it writes are the linker's: atoms' address (an image offset, the address less the base) and symbols'
// reference, got_slot and, in a PIE, dynamic_index (a dynamic symbol's or an export's .dynsym index, 0 for the others).
// A symbol's reference is its image offset, or a TLS symbol's offset from the thread pointer (variant II: from the end
// of the TLS block, which is the TLS segment's memory size rounded up to its alignment, since the segment starts on a
// multiple of its alignment), or for a weak symbol defined nowhere the image offset of address 0, or 0 if the symbol is
// thread-local, as GNU ld resolves one; in a PIE, a dynamic symbol's is its stub's image offset, or address 0's. Every
// fixup's field is patched in place, in its atom's bytes.
//
// Patching and writing go a chunk at a time (BUSTER_EXECUTABLE_CHUNK_BYTES, about 256 KB, a quarter of Zen 5's L2), in
// chunks that never split a field: boundary k is at k chunks' bytes, unless an atom of at most a chunk holds it, which
// then ends there, or a larger atom's field does, which then ends there. The boundaries depend on the layout alone, not
// on the lanes. For each chunk, a lane patches the fields in it (executable_patch_pc32 does CALL and PC32, and
// executable_patch_other redoes every other kind after it), hashes it while it is in L2 and writes it from there with
// pwritev. Every lane writes the chunks it patches: the writes take turns at the file's lock in the kernel, but each
// lane's overlaps the others' patching (docs/object-writer-and-linker.md, "Patching and writing", has what one lane
// writing them all costs instead). The build ID is a tree of the chunks' hashes: the hash of the chunks' hashes in
// chunk order, so that the lanes need nothing from each other to compute it and it doesn't depend on how many there
// are. Its note is hashed as zeros and written last.
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
// opens the output for every lane, and writes the build ID and closes it. The output is byte-identical for every lane
// count.
//
// main.c includes this file after lane.c, x86_64_encoder.c and link.c, and its String8. Layout, in this order:
//   - API: the base, the chunks, ExecutableSection, ExecutableError, ExecutableWrite, ExecutablePhase,
//     ExecutableDynamic, ExecutableOptions, ExecutableResult and executable_write's prototype.
//   - The chunk hash: ExecutableHash, executable_hash_begin, executable_hash_bytes and executable_hash_end.
//   - Patching: ExecutablePatch, executable_patch_pc32, executable_patch_other and executable_patch_range.
//   - The file: executable_write_all, executable_open and executable_close.
//   - Layout: the buckets (executable_bucket), ExecutableLayout and executable_lay_out.
//   - Headers: executable_write_headers.
//   - Chunks: executable_chunk_boundary, executable_patch_chunk and executable_chunk_runs.
//   - Writing: ExecutableShared; the relocation classes (executable_relocation_class), the filter (ExecutableFilter)
//     and a PIE's symbols (executable_mark_symbols); executable_count and executable_reduce_got; RELR (ExecutableRelr)
//     and executable_classify; executable_check_relocations; .dynamic and the totals (executable_dynamic_totals);
//     executable_place and executable_resolve; the relocations (executable_dynamic_relocations and
//     executable_relr_relocations) and executable_dynamic_sections; executable_chunks and executable_write.

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
    // In a static executable, a symbol of a shared library; or one the linker should define that it doesn't know
    // (ExecutableResult.symbol).
    EXECUTABLE_ERROR_UNSUPPORTED,
    // The entry point isn't a symbol defined in a TEXT atom.
    EXECUTABLE_ERROR_ENTRY,
    // A dynamic PIE can't relocate the fixup: an ABS64 that needs a relocation in an atom the loader doesn't write (a
    // text relocation), an ABS32 or ABS32S that needs one (no load address fits it), a fixup to a dynamic symbol of
    // another kind than a GOT kind, ABS64 and CALL, or a PC32 or PC64 to address 0 (ExecutableResult.fixup).
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
// plain data: the program interpreter, the library that the imports come from (libc.so.6) with its versions, a note to
// repeat (Scrt1.o's .note.ABI-tag), DT_INIT's and DT_FINI's functions (crti.o's _init and _fini) and the definitions to
// export (Bind.exports). The imports are the link's symbols of source RUNTIME_SHARED, each at the version its column
// holds (an ExecutableVersion's index, or 0 for none).
typedef struct ExecutableVersion ExecutableVersion;
struct ExecutableVersion
{
    String8 name;
    // The name's ELF hash: the library's vd_hash, which the executable's vna_hash repeats.
    uint32_t hash;
    // Its index among the library's version definitions.
    uint16_t index;
};

typedef struct ExecutableDynamic ExecutableDynamic;
struct ExecutableDynamic
{
    String8 interpreter;
    String8 needed;
    const ExecutableVersion* versions;
    uint32_t version_count;
    String8 note;
    SymbolId init;
    SymbolId fini;
    const SymbolId* exports;
    uint32_t export_count;
    // The library's index of GLIBC_ABI_DT_RELR, whose definition says that its loader takes DT_RELR, or 0: with it,
    // the executable's RELATIVE relocations at even addresses are RELR's, and it needs the version.
    uint16_t relr_version;
};

typedef struct ExecutableOptions ExecutableOptions;
struct ExecutableOptions
{
    // The output's path, which needn't be followed by a NUL.
    String8 path;
    // The entry point: a symbol defined in a TEXT atom (_start).
    SymbolId entry;
    // For a dynamic PIE, what it has besides; 0 for a static executable.
    const ExecutableDynamic* dynamic;
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
// fixups are CALL and PC32: S + A - P into 4 bytes, S being the target's reference. executable_patch_pc32 does every
// fixup of a range that way, in a loop built for those (0.38 ns a fixup in L1 and L2 on the 9700X, against 0.42 with a
// branch on the kind per fixup and 0.60 with a range check of each value). executable_patch_other then redoes every
// fixup of another kind, or with a wide addend, over what the loop wrote, which its field covers: a vector compare of
// the kind column finds them. The loop doesn't check S + A - P's range: executable_write does that once for every
// fixup, from the largest addend and the image's size, and runs every fixup through executable_patch_other when it
// can't rule out a value outside int32.

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

// Fixup `fixup` of an atom at image offset `address`, of any kind and either addend: whether its value fits its field.
// The patch comes by value, so that once inlined its columns stay in registers across the stores to the bytes, which
// could alias anything.
BUSTER_INLINE bool executable_patch_other(ExecutablePatch patch, uint8_t* bytes, uint32_t address, FixupId fixup)
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
        default:
            // link_check refuses other kinds.
            width = 0;
            break;
    }
    const bool fits = width == 8 || (width == 4 && (zero_extended ? (uint64_t)value <= UINT32_MAX : value == (int32_t)value));
    // A value that doesn't fit fails the link, and its field is zeroed over what executable_patch_pc32 wrote, so that
    // either path leaves the same bytes.
    value = fits ? value : 0;
    if (width == 8)
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

// Patches fixups [begin, end) of an atom at image offset `address`, all within `bytes`. Returns the first fixup whose
// value doesn't fit its field, or 0.
static FixupId executable_patch_range(const ExecutablePatch* patch, uint8_t* bytes, uint32_t address, FixupId begin, FixupId end)
{
    const ExecutablePatch local = *patch;
    FixupId result = 0;
    if (local.checked)
    {
        for (FixupId i = begin; i < end; i += 1)
        {
            const bool fits = executable_patch_other(local, bytes, address, i);
            result = result == 0 && !fits ? i : result;
        }
    }
    else
    {
        executable_patch_pc32(&local, bytes, address, begin, end);
        for (FixupId group = begin; group < end; group += 64)
        {
            uint64_t others = executable_other_kinds(local.kinds, group, end);
            while (others != 0)
            {
                const FixupId fixup = group + (FixupId)__builtin_ctzll(others);
                const bool fits = executable_patch_other(local, bytes, address, fixup);
                result = result == 0 && !fits ? fixup : result;
                others &= others - 1;
            }
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

// The section each class's atoms go to: in a static executable, EXECUTABLE_SECTION_COUNT for the C runtime's classes
// (.init's and .fini's), which an executable without libc doesn't have; in a dynamic PIE, every class's.
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

static const uint8_t executable_dynamic_class_sections[BUSTER_LINK_CLASS_COUNT] = {
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

static uint8_t executable_class_section(uint8_t class, bool dynamic)
{
    return dynamic ? executable_dynamic_class_sections[class] : executable_class_sections[class];
}

// Whether a class's atoms keep their id order without padding between them, as fragments of one piece: .init's and
// .fini's (a dynamic PIE's), which run into each other, and .eh_frame's, whose records follow one another. Each must
// start at a multiple of its own alignment where the ones before it end, but .eh_frame's at a multiple of 4: its
// records' lengths are, and lld writes the records one after another whatever their sections' alignment.
static bool executable_class_is_ordered(uint8_t class, bool dynamic)
{
    return class == BUSTER_LINK_CLASS_EH_FRAME || (dynamic && (class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI));
}

// An atom's alignment as its bucket has it, from its class, alignment and size, or BUSTER_EXECUTABLE_ALIGNMENTS if it
// isn't one the layout takes. An ordered class's atoms all go in its bucket of a byte.
static uint32_t executable_alignment_log2(uint8_t class, uint8_t alignment_log2, uint64_t size, bool dynamic)
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
    else if (executable_class_is_ordered(class, dynamic))
    {
        result = result <= BUSTER_EXECUTABLE_PAGE_LOG2 ? 0 : BUSTER_EXECUTABLE_ALIGNMENTS;
    }
    else if (executable_class_section(class, dynamic) == EXECUTABLE_SECTION_COUNT)
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

// A piece of the file: an atom's bytes, or bytes of the linker's own (atom 0).
typedef struct ExecutablePiece ExecutablePiece;
struct ExecutablePiece
{
    uint64_t offset;
    uint64_t size;
    uint8_t* bytes;
    AtomId atom;
};

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

// Program headers: PT_PHDR, a dynamic PIE's PT_INTERP, a PT_LOAD per segment with bytes, PT_TLS with TLS, a dynamic
// PIE's PT_DYNAMIC, PT_GNU_RELRO with a RELRO segment, PT_GNU_EH_FRAME with .eh_frame_hdr, PT_GNU_STACK and PT_NOTE.
#define BUSTER_EXECUTABLE_MAX_PROGRAM_HEADERS 12
// Section headers: the null one, the sections with bytes or memory, and .shstrtab.
#define BUSTER_EXECUTABLE_MAX_SECTION_HEADERS (EXECUTABLE_SECTION_COUNT + 2)
#define BUSTER_EXECUTABLE_SHSTRTAB_BYTES 272

// What every lane computes alike from the counts: where the sections, the segments and each bucket go.
typedef struct ExecutableLayout ExecutableLayout;
struct ExecutableLayout
{
    // Whether it is a dynamic PIE, and the image's base address.
    bool dynamic;
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
    uint64_t got_slots;
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

// The counts of one lane's atoms: per bucket, how many, their padded bytes and how many go in the file; per ordered
// class (executable_class_is_ordered), its atoms' largest alignment; and the FDEs in its .eh_frame atoms.
typedef struct ExecutableCounts ExecutableCounts;
struct ExecutableCounts
{
    uint64_t bytes[BUSTER_EXECUTABLE_BUCKETS];
    uint64_t pieces[BUSTER_EXECUTABLE_BUCKETS];
    uint8_t ordered_log2[BUSTER_LINK_CLASS_COUNT];
    // The FDEs of the lane's .eh_frame atoms.
    uint64_t fdes;
};

// Places the sections, from every lane's counts, the GOT's slots and the sizes of the linker's other sections
// (`linker_bytes`, by section, or 0 for none: a static executable's): the buckets one after another within their
// sections, each section at its alignment, and the segments 4 KB-congruent from the next page. Returns false if the
// image reaches BUSTER_EXECUTABLE_MAX_ADDRESS or the TLS block is larger than BUSTER_EXECUTABLE_MAX_TLS_BYTES. The
// atoms are at most BUSTER_EXECUTABLE_MAX_ATOM_BYTES each, so no sum of their sizes wraps.
static bool executable_lay_out(ExecutableLayout* layout, const ExecutableCounts* counts, uint32_t lanes, uint64_t got_slots, bool dynamic, const uint64_t* linker_bytes)
{
    *layout = (ExecutableLayout){ 0 };
    layout->dynamic = dynamic;
    layout->base = dynamic ? 0 : BUSTER_EXECUTABLE_BASE;
    uint64_t bytes[BUSTER_EXECUTABLE_BUCKETS] = { 0 };
    uint64_t pieces[BUSTER_EXECUTABLE_BUCKETS] = { 0 };
    uint8_t ordered_log2[BUSTER_LINK_CLASS_COUNT] = { 0 };
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        for (uint32_t bucket = 0; bucket < BUSTER_EXECUTABLE_BUCKETS; bucket += 1)
        {
            bytes[bucket] += counts[lane].bytes[bucket];
            pieces[bucket] += counts[lane].pieces[bucket];
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
    // largest, though they all go in its bucket of a byte.
    uint64_t strings_start = 0;
    for (uint8_t class = 1; class < BUSTER_LINK_CLASS_COUNT; class += 1)
    {
        const uint8_t section = executable_class_section(class, dynamic);
        uint64_t class_alignment = 0;
        uint64_t class_bytes = 0;
        for (uint32_t log2 = BUSTER_EXECUTABLE_PAGE_LOG2 + 1; log2-- > 0;)
        {
            const uint32_t bucket = executable_bucket(class, log2);
            class_alignment = class_alignment == 0 && bytes[bucket] != 0 ? 1ull << log2 : class_alignment;
            class_bytes += bytes[bucket];
        }
        class_alignment = class_alignment != 0 && executable_class_is_ordered(class, dynamic) ? 1ull << ordered_log2[class] : class_alignment;
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
    layout->program_headers = 4 + (has_tls ? 1 : 0) + (layout->has_segment[EXECUTABLE_SEGMENT_RELRO] ? 1 : 0) + (dynamic ? 2 : 0) +
                              (section_bytes[EXECUTABLE_SECTION_EH_FRAME_HDR] != 0 ? 1 : 0);
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
        const uint8_t section = executable_class_section(class, dynamic);
        uint64_t cursor = section != EXECUTABLE_SECTION_COUNT ? layout->sections[section].address + (class == BUSTER_LINK_CLASS_STRINGS ? strings_start : 0) : 0;
        for (uint32_t log2 = BUSTER_EXECUTABLE_PAGE_LOG2 + 1; log2-- > 0;)
        {
            const uint32_t bucket = executable_bucket(class, log2);
            cursor = bytes[bucket] != 0 ? executable_align(cursor, executable_class_is_ordered(class, dynamic) ? 1ull << ordered_log2[class] : 1ull << log2) : cursor;
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
        layout->shstrtab_size += present ? strlen(executable_section_names[section]) + 1 : 0;
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

// The headers, in lld's order of program headers: PT_PHDR, a dynamic PIE's PT_INTERP, the PT_LOADs, PT_TLS, a dynamic
// PIE's PT_DYNAMIC, PT_GNU_RELRO, PT_GNU_EH_FRAME, PT_GNU_STACK (not executable) and PT_NOTE (the ABI note and the
// build ID's, which follow each other). The note's hash stays zero until the build ID is known.
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
    if (layout->dynamic)
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
            const uint32_t length = (uint32_t)strlen(executable_section_names[section]);
            memcpy(headers->shstrtab + name, executable_section_names[section], length + 1);
            const uint8_t linked = executable_section_links[section];
            headers->section_headers[index] = (Elf64_Shdr){
                .sh_name = name,
                .sh_type = executable_section_types[section],
                .sh_flags = executable_section_flags[section],
                .sh_addr = layout->base + placed->address,
                .sh_offset = placed->offset,
                .sh_size = placed->size,
                .sh_link = linked != 0 ? layout->section_index[linked] : 0,
                .sh_info = section == EXECUTABLE_SECTION_DYNSYM || section == EXECUTABLE_SECTION_VERNEED ? 1 : 0,
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

// Where the chunk boundary for `nominal` (a multiple of BUSTER_EXECUTABLE_CHUNK_BYTES) is: there, unless it falls
// inside an atom. An atom of at most a chunk ends the chunk at its end; a larger one is cut there, unless a field
// crosses it, and then at the field's end. No field crosses an atom's end, and the linker's pieces have none.
static uint64_t executable_chunk_boundary(const Link* link, const ExecutablePiece* pieces, uint64_t piece_count, uint64_t nominal)
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
            // The field before the cut, if any: the last one that starts before it.
            const uint64_t cut = nominal - piece->offset;
            const FixupId begin = link->atoms.fixup_begin[atom];
            const FixupId end = begin + link->atoms.fixup_count[atom];
            const FixupId after = executable_fixup_at(link->fixups.offset, begin, end, cut);
            if (after > begin)
            {
                const FixupId before = after - 1;
                const uint8_t kind = link->fixups.kind[before] & BUSTER_LINK_FIXUP_KIND;
                const uint64_t field_end = (uint64_t)link->fixups.offset[before] + link_fixup_kinds[kind].width;
                result = field_end > cut ? piece->offset + field_end : nominal;
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
//     of their FDEs, as in a link of a compiler's objects; only if they aren't does it sort them, which the exact twin
//     always does (executable_eh_frame_sort).

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
// the pieces before the GOT's piece and after it.
typedef struct ExecutableDynamicCounts ExecutableDynamicCounts;
struct ExecutableDynamicCounts
{
    // On a line of its own, since each lane writes its own.
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint64_t stubs;
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
    uint64_t needed_name;
    uint64_t version_names;
    uint64_t got_relative;
    uint64_t got_symbolic;
    uint64_t abs64_relative;
    uint64_t abs64_symbolic;
    uint64_t relr_relocations;
    uint64_t relr_words;
    uint64_t versions;
    uint64_t buckets;
    uint64_t bloom_words;
    uint32_t dynamic_entries;
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
    // Whether the passes go by their exact twins (ExecutableOptions.exact). A dynamic PIE's (0 for a static
    // executable): the bitsets of the symbols by relocation class (the dynamic ones, SYMBOLIC, and the RELATIVE ones),
    // words_per_lane words, each lane writing its range of words; each lane's part of the filter
    // (BUSTER_EXECUTABLE_FILTER_WORDS words each); the lanes' counts; each lane's bitset of the symbols that have stubs
    // (words_per_lane words each, reduced into row 0 as the GOT's are); each lane's bitset of the library's version
    // indices that its imports need (version_words words of indices below version_limit each), and lane 0's numbers for
    // them in the executable (from 2, 0 for an index no import needs); and with RELR, the RELR words of each atom the
    // loader writes (executable_class_is_written; the others' are never read), with BUSTER_EXECUTABLE_RELR_DENSE and
    // BUSTER_EXECUTABLE_RELR_RELA.
    bool exact;
    bool relr;
    uint64_t* dynamic_bits;
    uint64_t* relative_bits;
    uint32_t* filters;
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
    // holds them all. Then each lane's count of them in its range of words, and its largest addend magnitude.
    uint64_t* got_marks;
    uint64_t words_per_lane;
    uint64_t* got_counts;
    uint64_t* addend_magnitudes;
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

// A symbol's reference (see the orientation comment), or the error it gets. A dynamic PIE's imports, and the symbols
// that its stubs stand for, get theirs from executable_resolve and executable_place.
static ExecutableError executable_reference(const Link* link, const ExecutableLayout* layout, SymbolId symbol, uint32_t* reference)
{
    const AtomId atom = link->symbols.atom[symbol];
    const uint8_t source = link->symbols.source[symbol];
    const bool weak = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const ExecutableSectionLayout* tdata = &layout->sections[EXECUTABLE_SECTION_TDATA];
    const ExecutableSectionLayout* init = &layout->sections[EXECUTABLE_SECTION_INIT_ARRAY];
    const ExecutableSectionLayout* fini = &layout->sections[EXECUTABLE_SECTION_FINI_ARRAY];
    ExecutableError result = EXECUTABLE_ERROR_NONE;
    if (atom != 0 && link_class_is_tls(link->atoms.class[atom]))
    {
        *reference = (uint32_t)(link->atoms.address[atom] + link->symbols.value[symbol] - tdata->address - layout->tls_block);
    }
    else if (atom != 0)
    {
        *reference = (uint32_t)(link->atoms.address[atom] + link->symbols.value[symbol]);
    }
    else if (source == BUSTER_LINK_SOURCE_LINKER && executable_named(link, symbol, "__init_array_start"))
    {
        *reference = (uint32_t)init->address;
    }
    else if (source == BUSTER_LINK_SOURCE_LINKER && executable_named(link, symbol, "__init_array_end"))
    {
        *reference = (uint32_t)(init->address + init->size);
    }
    else if (source == BUSTER_LINK_SOURCE_LINKER && executable_named(link, symbol, "__fini_array_start"))
    {
        *reference = (uint32_t)fini->address;
    }
    else if (source == BUSTER_LINK_SOURCE_LINKER && executable_named(link, symbol, "__fini_array_end"))
    {
        *reference = (uint32_t)(fini->address + fini->size);
    }
    else if (source == BUSTER_LINK_SOURCE_LINKER && executable_named(link, symbol, "_GLOBAL_OFFSET_TABLE_"))
    {
        *reference = (uint32_t)layout->sections[EXECUTABLE_SECTION_GOT].address;
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

// A dynamic PIE's relocation class of a symbol: what its address needs when the program loads. Nothing for a TLS
// symbol (its offset from the thread pointer is the executable's to know) or a weak reference to nothing that isn't
// dynamic (address 0); RELATIVE for a symbol of the image's, to which the load address adds; SYMBOLIC for an import,
// and for a weak reference to nothing with default visibility and a name, which a library may define, as GNU ld and lld
// make it: the loader looks them up in .dynsym. The SYMBOLIC ones are .dynsym's undefined entries.
#define BUSTER_EXECUTABLE_RELOCATION_NONE 0
#define BUSTER_EXECUTABLE_RELOCATION_RELATIVE 1
#define BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC 2

static uint8_t executable_relocation_class(const Link* link, SymbolId symbol)
{
    const LinkSymbols* symbols = &link->symbols;
    const uint8_t flags = symbols->flags[symbol];
    const uint8_t source = symbols->source[symbol];
    const bool tls = executable_is_tls(link, symbol);
    const bool nowhere = source == BUSTER_LINK_SOURCE_NONE && symbols->atom[symbol] == 0;
    const bool weak = (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const bool visible = (flags & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_DEFAULT;
    uint8_t result = BUSTER_EXECUTABLE_RELOCATION_RELATIVE;
    if (source == BUSTER_LINK_SOURCE_RUNTIME_SHARED || (nowhere && weak && visible && !tls && symbols->name[symbol] != 0))
    {
        result = BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC;
    }
    else if (tls || nowhere)
    {
        result = BUSTER_EXECUTABLE_RELOCATION_NONE;
    }
    return result;
}

// Whether a dynamic PIE's symbol is at address 0 wherever the image loads: a weak reference to nothing that isn't dynamic
// or thread-local (relocation class NONE), as hidden ones are. Its reference is address 0's image offset, right for an
// absolute field, which no relocation then changes, but not for a field measured from where it is: the loader moves the
// field, and not address 0.
static bool executable_is_absolute_zero(const Link* link, SymbolId symbol)
{
    const bool weak = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    return weak && link->symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE && link->symbols.atom[symbol] == 0 && !executable_is_tls(link, symbol) &&
           executable_relocation_class(link, symbol) == BUSTER_EXECUTABLE_RELOCATION_NONE;
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

// A dynamic PIE's filter of the symbols that the image doesn't define (sources NONE and RUNTIME_SHARED: weak references
// to nothing and imports): a bit per hash of each, BUSTER_EXECUTABLE_FILTER_WORDS 32-bit words in two registers, so
// that 16 fixups' targets are tested at once without a load ("permutes as table lookups" in
// docs/object-writer-and-linker.md). A clear bit means that the image defines the target, so that no fixup to it needs
// a stub and an ABS64 to it is RELATIVE. A set bit can be another symbol's (about the share of the 1,024 bits that are
// set: a few hundredths with tens of such symbols), which the exact check tells apart.
#define BUSTER_EXECUTABLE_FILTER_WORDS 32
#define BUSTER_EXECUTABLE_FILTER_MULTIPLIER 0x9e3779b1u

typedef struct ExecutableFilter ExecutableFilter;
struct ExecutableFilter
{
    __m512i low;
    __m512i high;
};

static uint32_t executable_filter_bit(uint32_t symbol)
{
    return (symbol * BUSTER_EXECUTABLE_FILTER_MULTIPLIER) >> 22;
}

// Of the targets in `lanes`, those whose bits are set.
static __mmask16 executable_filter_test(const ExecutableFilter* filter, __m512i targets, __mmask16 lanes)
{
    const __m512i bits = _mm512_srli_epi32(_mm512_mullo_epi32(targets, _mm512_set1_epi32((int)BUSTER_EXECUTABLE_FILTER_MULTIPLIER)), 22);
    const __m512i words = _mm512_permutex2var_epi32(filter->low, _mm512_srli_epi32(bits, 5), filter->high);
    const __m512i shifted = _mm512_srlv_epi32(words, _mm512_and_si512(bits, _mm512_set1_epi32(31)));
    return _mm512_mask_test_epi32_mask(lanes, shifted, _mm512_set1_epi32(1));
}

// The lanes' parts of the filter, together.
static ExecutableFilter executable_filter(const ExecutableShared* shared, uint32_t lanes)
{
    ExecutableFilter result = {
        .low = _mm512_setzero_si512(),
        .high = _mm512_setzero_si512(),
    };
    for (uint32_t lane = 0; shared->filters != 0 && lane < lanes; lane += 1)
    {
        const uint32_t* part = shared->filters + BUSTER_EXECUTABLE_FILTER_WORDS * lane;
        result.low = _mm512_or_si512(result.low, _mm512_load_si512(part));
        result.high = _mm512_or_si512(result.high, _mm512_load_si512(part + 16));
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

// A dynamic PIE's symbols, 64 at a time over this lane's range of words of the bitsets: the SYMBOLIC and RELATIVE ones
// (executable_relocation_class's), each set of bits a word of its bitset; this lane's part of the filter; and what the
// dynamic ones add to its counts, one at a time. The classes are lane masks over the source and flags columns, which
// link_check makes enough: a symbol of source NONE has no atom, and a TLS symbol is one by its type, its atom's class's.
// Only the words with a weak reference to nothing of default visibility read the names (whether it has one). With
// `exact`, its twin: executable_relocation_class of one symbol at a time.
static void executable_mark_symbols(const Link* link, ExecutableShared* shared, uint32_t lane)
{
    const LinkSymbols* symbols = &link->symbols;
    shared->dynamic_counts[lane] = (ExecutableDynamicCounts){ 0 };
    uint32_t* filter = shared->filters + BUSTER_EXECUTABLE_FILTER_WORDS * lane;
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
                const uint8_t class = symbol_valid ? executable_relocation_class(link, symbol) : BUSTER_EXECUTABLE_RELOCATION_NONE;
                const bool is_outside = symbol_valid && (symbols->source[symbol] == BUSTER_LINK_SOURCE_NONE || symbols->source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED);
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
            const uint64_t weak = _mm512_cmpeq_epi8_mask(_mm512_and_si512(flags, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_BINDING)), _mm512_set1_epi8(BUSTER_LINK_SYMBOL_WEAK));
            const uint64_t visible = _mm512_cmpeq_epi8_mask(_mm512_and_si512(flags, _mm512_set1_epi8((char)BUSTER_LINK_SYMBOL_VISIBILITY)),
                                                            _mm512_set1_epi8(BUSTER_LINK_SYMBOL_DEFAULT));
            const uint64_t tls = _mm512_cmpeq_epi8_mask(_mm512_and_si512(flags, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_TYPE)), _mm512_set1_epi8(BUSTER_LINK_SYMBOL_TLS));
            const uint64_t candidates = nowhere & weak & visible & ~tls;
            uint64_t named = 0;
            for (uint32_t part = 0; candidates != 0 && part < 4; part += 1)
            {
                const __mmask16 lanes = (__mmask16)(candidates >> (16 * part));
                const __m512i names = _mm512_maskz_loadu_epi32(lanes, symbols->name + first + 16 * part);
                named |= (uint64_t)_mm512_mask_cmpneq_epi32_mask(lanes, names, _mm512_setzero_si512()) << (16 * part);
            }
            dynamic = import | (candidates & named);
            relative = valid & ~dynamic & ~tls & ~nowhere;
            outside = import | nowhere;
        }
        shared->dynamic_bits[word] = dynamic;
        shared->relative_bits[word] = relative;
        for (uint64_t left = dynamic; left != 0; left &= left - 1)
        {
            executable_count_dynamic_symbol(link, shared, lane, (SymbolId)(first + (uint64_t)__builtin_ctzll(left)));
        }
        for (uint64_t left = outside; left != 0; left &= left - 1)
        {
            const uint32_t bit = executable_filter_bit((uint32_t)(first + (uint64_t)__builtin_ctzll(left)));
            filter[bit / 32] |= 1u << (bit % 32);
        }
    }
}

// A dynamic PIE's fixup that doesn't read a GOT slot and isn't ABS64, whose target the filter (or with `exact`, its
// atom) says may be outside the image. A CALL to a dynamic symbol goes to the symbol's stub, which reads its GOT slot:
// both are marked in this lane's bitsets. Any other kind of fixup to a dynamic symbol can't be relocated (lld refuses it
// too: the code isn't position-independent), and nor can a PC32 or PC64 to address 0 (executable_is_absolute_zero); they
// are only counted here, and executable_check_relocations reports them. A CALL to address 0 is patched, as GNU ld and
// lld patch it: whatever it reaches, the program can only call it after checking that the symbol has an address.
static void executable_check_direct(const Link* link, ExecutableShared* shared, uint32_t lane, FixupId fixup, uint64_t* refused)
{
    const SymbolId target = link->fixups.target[fixup];
    const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
    const bool call = kind == BUSTER_LINK_FIXUP_CALL;
    const bool relative = kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_PC64;
    const bool dynamic = executable_bit(shared->dynamic_bits, target);
    const uint64_t word = lane * shared->words_per_lane + target / 64;
    const uint64_t bit = dynamic && call ? 1ull << (target % 64) : 0;
    shared->got_marks[word] |= bit;
    shared->stub_marks[word] |= bit;
    *refused += (dynamic && !call) || (relative && !dynamic && executable_is_absolute_zero(link, target)) ? 1 : 0;
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
// of the GOT kinds among its fixups, whose largest addend magnitude it records. For a dynamic PIE, after
// executable_mark_symbols, it also counts its ABS64 fixups and its ABS32 and ABS32S ones, and tests the targets of the
// others against `filter`, 16 at a time, sending those it passes to executable_check_direct (with `exact`, those whose
// atom is 0, one at a time). Failures go to `error`.
static void executable_count(Link* link, ExecutableShared* shared, uint32_t lane, bool dynamic, const ExecutableFilter* filter, ExecutableLaneError* error)
{
    const LinkAtoms* atoms = &link->atoms;
    ExecutableCounts* counts = &shared->counts[lane];
    memset(counts, 0, sizeof(*counts));
    // The columns in locals, which the stores to the counts can't alias. A run of atoms of one bucket, as consecutive
    // atoms mostly are, is summed in registers: in the counts, each atom's add would wait for the last one's store.
    const uint8_t* class_of = atoms->class;
    const uint8_t* alignment_of = atoms->alignment_log2;
    const uint64_t* size_of = atoms->size;
    uint32_t run = 0;
    uint64_t run_bytes = 0;
    uint64_t run_pieces = 0;
    const Range64 atom_range = lane_range(atoms->count - 1);
    for (AtomId atom = (AtomId)atom_range.begin + 1; atom < atom_range.end + 1; atom += 1)
    {
        const uint8_t class = class_of[atom];
        const uint64_t size = size_of[atom];
        const uint32_t log2 = executable_alignment_log2(class, alignment_of[atom], size, dynamic);
        // Bounded before its padding is added, which could wrap: up to 2^32 atoms of 2 GB each sum to at most 2^63.
        if (log2 == BUSTER_EXECUTABLE_ALIGNMENTS || size > BUSTER_EXECUTABLE_MAX_ATOM_BYTES)
        {
            executable_lane_fail(error, EXECUTABLE_ERROR_LAYOUT, atom, 0, 0);
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
            if (executable_class_is_ordered(class, dynamic) && alignment_of[atom] > counts->ordered_log2[class])
            {
                counts->ordered_log2[class] = alignment_of[atom];
            }
        }
    }
    counts->bytes[run] += run_bytes;
    counts->pieces[run] += run_pieces;

    const LinkFixups* fixups = &link->fixups;
    uint64_t* marks = shared->got_marks + lane * shared->words_per_lane;
    memset(marks, 0, shared->words_per_lane * sizeof(*marks));
    if (dynamic)
    {
        memset(shared->stub_marks + lane * shared->words_per_lane, 0, shared->words_per_lane * sizeof(uint64_t));
    }
    // 64 fixups at a time: a compare of their kinds finds the GOT kinds' (CALL_GOT, LOAD_GOT and GOTPCREL are 2 to 4,
    // and GOTTPOFF), whose targets get their bits one at a time, and the addend column's magnitudes go into a maximum.
    // Masked loads stay within the columns.
    const Range64 fixup_range = lane_range(fixups->count - 1);
    const FixupId end = (FixupId)fixup_range.end + 1;
    __m512i largest = _mm512_setzero_si512();
    uint64_t abs64 = 0;
    uint64_t absolute32 = 0;
    uint64_t refused = 0;
    for (FixupId group = (FixupId)fixup_range.begin + 1; group < end; group += 64)
    {
        const uint64_t count = end - group;
        const __mmask64 valid = count >= 64 ? ~0ull : (1ull << count) - 1;
        const __m512i kinds = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, fixups->kind + group), _mm512_set1_epi8(BUSTER_LINK_FIXUP_KIND));
        const __mmask64 slot_reads = _mm512_cmple_epu8_mask(_mm512_sub_epi8(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_CALL_GOT)), _mm512_set1_epi8(2));
        const uint64_t through_got = valid & (slot_reads | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_GOTTPOFF)));
        for (uint64_t got = through_got; got != 0; got &= got - 1)
        {
            const SymbolId target = fixups->target[group + (FixupId)__builtin_ctzll(got)];
            marks[target / 64] |= 1ull << (target % 64);
        }
        for (uint32_t part = 0; part < 4; part += 1)
        {
            const __mmask16 lanes = (__mmask16)(valid >> (16 * part));
            const __m512i addends = _mm512_maskz_loadu_epi32(lanes, fixups->addend + group + 16 * part);
            largest = _mm512_max_epu32(largest, _mm512_abs_epi32(addends));
        }
        if (dynamic)
        {
            const uint64_t abs64_mask = valid & _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_ABS64));
            const uint64_t absolute32_mask =
                valid & (_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_ABS32)) | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_ABS32S)));
            abs64 += (uint64_t)__builtin_popcountll(abs64_mask);
            absolute32 += (uint64_t)__builtin_popcountll(absolute32_mask);
            const uint64_t direct = valid & ~(through_got | abs64_mask);
            for (uint32_t part = 0; !shared->exact && part < 4; part += 1)
            {
                const __mmask16 lanes = (__mmask16)(direct >> (16 * part));
                const __m512i targets = _mm512_maskz_loadu_epi32(lanes, fixups->target + group + 16 * part);
                for (uint32_t hits = executable_filter_test(filter, targets, lanes); hits != 0; hits &= hits - 1)
                {
                    executable_check_direct(link, shared, lane, group + 16 * part + (FixupId)__builtin_ctz(hits), &refused);
                }
            }
            for (uint64_t left = shared->exact ? direct : 0; left != 0; left &= left - 1)
            {
                const FixupId fixup = group + (FixupId)__builtin_ctzll(left);
                if (link->symbols.atom[fixups->target[fixup]] == 0)
                {
                    executable_check_direct(link, shared, lane, fixup, &refused);
                }
            }
        }
    }
    // INT32_MIN's absolute value reads as 2^31, unsigned.
    shared->addend_magnitudes[lane] = _mm512_reduce_max_epu32(largest);
    if (dynamic)
    {
        ExecutableDynamicCounts* dynamic_counts = &shared->dynamic_counts[lane];
        dynamic_counts->abs64_fixups = abs64;
        dynamic_counts->absolute32 = absolute32;
        dynamic_counts->refused = refused;
    }
}

// Of a word of the GOT's marks, the symbols whose slots a dynamic PIE relocates by class: from the bitsets, or with
// `exact` executable_relocation_class of each.
static uint64_t executable_slots_of_class(const Link* link, const ExecutableShared* shared, uint64_t word, uint64_t marks, uint8_t class)
{
    uint64_t result = 0;
    if (shared->exact)
    {
        for (uint64_t left = marks; left != 0; left &= left - 1)
        {
            const uint64_t bit = (uint64_t)__builtin_ctzll(left);
            result |= executable_relocation_class(link, (SymbolId)(word * 64 + bit)) == class ? 1ull << bit : 0;
        }
    }
    else
    {
        result = marks & (class == BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC ? shared->dynamic_bits[word] : shared->relative_bits[word]);
    }
    return result;
}

// ORs every lane's bitset into lane 0's, over this lane's range of words, and counts the slots there; for a dynamic
// PIE, the same of the stubs' bitsets, and the GOT slots' relocations, RELATIVE and SYMBOLIC, the RELATIVE ones
// .relr.dyn's with RELR.
static void executable_reduce_got(const Link* link, ExecutableShared* shared, uint32_t lane, uint32_t lanes)
{
    const Range64 words = lane_range(shared->words_per_lane);
    const bool dynamic = shared->dynamic_bits != 0;
    const bool relr_slots = dynamic && shared->relr;
    uint64_t count = 0;
    uint64_t stubs = 0;
    uint64_t relative = 0;
    uint64_t symbolic = 0;
    uint64_t relr_relocations = 0;
    uint64_t relr_words = 0;
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        uint64_t marks = 0;
        uint64_t stub_marks = 0;
        for (uint32_t other = 0; other < lanes; other += 1)
        {
            marks |= shared->got_marks[other * shared->words_per_lane + word];
            stub_marks |= dynamic ? shared->stub_marks[other * shared->words_per_lane + word] : 0;
        }
        shared->got_marks[word] = marks;
        count += (uint64_t)__builtin_popcountll(marks);
        if (dynamic)
        {
            shared->stub_marks[word] = stub_marks;
            stubs += (uint64_t)__builtin_popcountll(stub_marks);
        }
        const uint64_t word_relative = dynamic ? (uint64_t)__builtin_popcountll(executable_slots_of_class(link, shared, word, marks, BUSTER_EXECUTABLE_RELOCATION_RELATIVE)) : 0;
        symbolic += dynamic ? (uint64_t)__builtin_popcountll(executable_slots_of_class(link, shared, word, marks, BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC)) : 0;
        // With RELR, a word's RELATIVE slots are .relr.dyn's: the slots of a word's marked symbols are consecutive, at
        // most 64, so they take an address entry and at most one bitmap (executable_got_relr).
        relative += relr_slots ? 0 : word_relative;
        relr_relocations += relr_slots ? word_relative : 0;
        relr_words += relr_slots ? (word_relative >= 1 ? 1 : 0) + (word_relative >= 2 ? 1 : 0) : 0;
    }
    shared->got_counts[lane] = count;
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
static uint8_t executable_abs64_class(const Link* link, SymbolId target, bool outside)
{
    return outside ? executable_relocation_class(link, target) : BUSTER_EXECUTABLE_RELOCATION_RELATIVE;
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
        const uint8_t class = abs64 ? executable_abs64_class(link, target, outside) : BUSTER_EXECUTABLE_RELOCATION_NONE;
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

// Whether a dynamic PIE can't relocate a fixup of `kind` to `target` in an atom of class `atom_class`: an ABS64 that
// needs a relocation outside the atoms the loader writes (a text relocation), an ABS32 or ABS32S that needs one (no load
// address fits it), a fixup of another kind to a dynamic symbol, but for a GOT kind and a CALL, which goes to the
// symbol's stub, or a PC32 or PC64 to address 0 (executable_is_absolute_zero).
static bool executable_is_refused(const Link* link, uint8_t kind, SymbolId target, uint8_t atom_class)
{
    const uint8_t class = executable_relocation_class(link, target);
    const bool relocated = class != BUSTER_EXECUTABLE_RELOCATION_NONE;
    const bool absolute32 = kind == BUSTER_LINK_FIXUP_ABS32 || kind == BUSTER_LINK_FIXUP_ABS32S;
    const bool text = kind == BUSTER_LINK_FIXUP_ABS64 && !executable_class_is_written(atom_class);
    const bool direct = !executable_kind_reads_got(kind) && kind != BUSTER_LINK_FIXUP_ABS64 && kind != BUSTER_LINK_FIXUP_CALL;
    const bool relative = kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_PC64;
    return (relocated && (text || absolute32)) || (direct && class == BUSTER_EXECUTABLE_RELOCATION_SYMBOLIC) || (relative && executable_is_absolute_zero(link, target));
}

// A dynamic PIE's fixups that it can't relocate (executable_is_refused), which the lanes' counts point to and lane 0
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
            failed = smaller && executable_is_refused(link, kind, fixups->target[fixup], atoms->class[atom]) ? fixup : failed;
        }
    }
    if (failed != 0)
    {
        executable_lane_fail(error, EXECUTABLE_ERROR_RELOCATION, 0, 0, failed);
    }
}

// .dynamic's entries, in lld's order: DT_NEEDED, DT_FLAGS_1 (a PIE), DT_DEBUG, the relocations' (.rela.dyn's, then
// .relr.dyn's), the symbols' and strings', DT_GNU_HASH, the init and fini arrays', DT_INIT and DT_FINI, the versions',
// and DT_NULL. With `entries` 0, only how many; otherwise their values from the layout, and DT_INIT's and DT_FINI's
// addresses.
#define BUSTER_EXECUTABLE_MAX_DYNAMIC_ENTRIES 28

static uint32_t executable_dynamic_entries(const ExecutableLayout* layout, const ExecutableDynamicTotals* totals, uint64_t init, uint64_t fini, Elf64_Dyn* entries)
{
    const ExecutableSectionLayout* sections = layout != 0 ? layout->sections : 0;
    const uint64_t base = layout != 0 ? layout->base : 0;
    const uint64_t relocations = totals->got_relative + totals->got_symbolic + totals->abs64_relative + totals->abs64_symbolic;
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
        { .d_tag = DT_NEEDED, .d_un.d_val = totals->needed_name },
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
        { .d_tag = totals->versions != 0 ? DT_VERNEEDNUM : DT_NULL, .d_un.d_val = 1 },
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

// A dynamic PIE's totals, the same on every lane, from the lanes' counts: what its sections hold, and their sizes for
// the layout. .gnu.hash has a bucket per four exports, at least one, and a Bloom filter of 12 bits per export in the
// next power of two of 64-bit words above them, as lld sizes them.
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
        totals->has_init_array = totals->has_init_array || shared->counts[lane].bytes[executable_bucket(BUSTER_LINK_CLASS_INIT_ARRAY, BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2)] != 0;
        totals->has_fini_array = totals->has_fini_array || shared->counts[lane].bytes[executable_bucket(BUSTER_LINK_CLASS_FINI_ARRAY, BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2)] != 0;
    }
    uint64_t version_name_bytes = 0;
    for (uint32_t v = 0; v < dynamic->version_count; v += 1)
    {
        const uint16_t index = dynamic->versions[v].index;
        totals->versions += shared->version_numbers[index] != 0 ? 1 : 0;
        version_name_bytes += shared->version_numbers[index] != 0 ? dynamic->versions[v].name.length + 1 : 0;
    }
    uint64_t export_bytes = 0;
    for (uint32_t e = 0; e < dynamic->export_count; e += 1)
    {
        export_bytes += (uint64_t)link->names.length[link->symbols.name[dynamic->exports[e]]] + 1;
    }
    totals->has_init = dynamic->init != 0;
    totals->has_fini = dynamic->fini != 0;
    totals->exports = dynamic->export_count;
    totals->export_names = 1 + name_bytes;
    totals->needed_name = totals->export_names + export_bytes;
    totals->version_names = totals->needed_name + dynamic->needed.length + 1;
    totals->buckets = totals->exports / 4 > 1 ? totals->exports / 4 : 1;
    uint64_t words = 1;
    while (totals->exports != 0 && words <= totals->exports * 12 / 64)
    {
        words *= 2;
    }
    totals->bloom_words = words;
    const uint64_t entries = 1 + totals->undefined + totals->exports;
    uint64_t* bytes = totals->linker_bytes;
    bytes[EXECUTABLE_SECTION_INTERP] = dynamic->interpreter.length + 1;
    bytes[EXECUTABLE_SECTION_ABI_NOTE] = dynamic->note.length;
    bytes[EXECUTABLE_SECTION_DYNSYM] = entries * sizeof(Elf64_Sym);
    bytes[EXECUTABLE_SECTION_VERSYM] = totals->versions != 0 ? entries * sizeof(Elf64_Half) : 0;
    bytes[EXECUTABLE_SECTION_VERNEED] = totals->versions != 0 ? sizeof(Elf64_Verneed) + totals->versions * sizeof(Elf64_Vernaux) : 0;
    bytes[EXECUTABLE_SECTION_GNU_HASH] = 16 + 8 * totals->bloom_words + 4 * totals->buckets + 4 * totals->exports;
    bytes[EXECUTABLE_SECTION_DYNSTR] = totals->version_names + version_name_bytes;
    bytes[EXECUTABLE_SECTION_RELA_DYN] = (totals->got_relative + totals->got_symbolic + totals->abs64_relative + totals->abs64_symbolic) * sizeof(Elf64_Rela);
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

// Fills this lane's atoms' addresses and pieces, and its range of the symbols' GOT slots and, in a dynamic PIE, the
// references of the symbols that have stubs (their stubs' addresses), from where the layout puts its buckets, its slots
// and its stubs: after the lanes' before it. An atom of an ordered class whose place isn't a multiple of its alignment
// fails in `error`.
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
    for (AtomId atom = (AtomId)atom_range.begin + 1; atom < atom_range.end + 1; atom += 1)
    {
        const uint8_t class = class_of[atom];
        const uint64_t size = size_of[atom];
        const uint32_t log2 = executable_alignment_log2(class, alignment_of[atom], size, layout->dynamic);
        const uint32_t bucket = executable_bucket(class, log2);
        if (bucket != run)
        {
            addresses[run] = run_address;
            pieces[run] = run_piece;
            run = bucket;
            run_address = addresses[bucket];
            run_piece = pieces[bucket];
        }
        address_of[atom] = run_address;
        // An ordered class's atoms follow one another without padding, so each must land on a multiple of its alignment
        // (the others' buckets keep theirs); .eh_frame's records need only 4 bytes whatever their atoms' alignment, as lld
        // writes them one after another.
        const uint32_t required = class == BUSTER_LINK_CLASS_EH_FRAME && alignment_of[atom] > 2 ? 2 : alignment_of[atom];
        if (executable_class_is_ordered(class, layout->dynamic) && (run_address & ((1ull << required) - 1)) != 0)
        {
            executable_lane_fail(error, EXECUTABLE_ERROR_LAYOUT, atom, 0, 0);
        }
        if (executable_in_file(class, size))
        {
            const uint8_t section = executable_class_section(class, layout->dynamic);
            placed[run_piece] = (ExecutablePiece){
                .offset = executable_file_offset(layout, section, run_address),
                .size = size,
                .bytes = bytes_of[atom],
                .atom = atom,
            };
            run_piece += 1;
        }
        run_address += executable_align(size, 1ull << log2);
    }

    uint32_t slot = 1;
    uint64_t stub = 0;
    for (uint32_t other = 0; other < lane; other += 1)
    {
        slot += (uint32_t)shared->got_counts[other];
        stub += layout->dynamic ? shared->dynamic_counts[other].stubs : 0;
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
        for (uint64_t left = layout->dynamic ? shared->stub_marks[word] : 0; left != 0; left &= left - 1)
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

// Fills this lane's range of the symbols' references and their GOT slots, and of the chunks' boundaries. Most symbols
// are in an atom that isn't thread-local, whose reference comes from the columns directly (in locals, which the stores
// to the references can't alias); executable_reference gives the others'. A dynamic PIE's SYMBOLIC symbols come after,
// by this lane's range of the bitsets' words (as executable_mark_symbols counts them): their stubs, their references (a
// stub's address from executable_place, or address 0) and their .dynsym entries, names and versions, after the lanes'
// before them, with their indices; the other symbols' indices are 0. Then the RELR words of this lane's range of the
// written pieces (executable_written_pieces) before and after the GOT's. Failures go to `error`.
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
    const uint64_t base = layout->base;
    const Range64 symbol_range = lane_range(symbols->count - 1);
    for (SymbolId symbol = (SymbolId)symbol_range.begin + 1; symbol < symbol_range.end + 1; symbol += 1)
    {
        const AtomId atom = atom_of[symbol];
        const bool plain = atom != 0 && !link_class_is_tls(class_of[atom]);
        const bool symbolic = !plain && dynamic && executable_bit(shared->dynamic_bits, symbol);
        uint32_t reference = plain ? (uint32_t)(address_of[atom] + value_of[symbol]) : 0;
        const ExecutableError failure = plain || symbolic ? EXECUTABLE_ERROR_NONE : executable_reference(link, layout, symbol, &reference);
        if (failure != EXECUTABLE_ERROR_NONE)
        {
            executable_lane_fail(error, failure, 0, symbol, 0);
        }
        if (!symbolic)
        {
            reference_of[symbol] = reference;
            const uint32_t slot = slot_of[symbol];
            if (slot != 0)
            {
                // A TLS symbol's slot holds its offset from the thread pointer (TPOFF64), another's its address: in a
                // dynamic PIE, the address at base 0, which a RELATIVE relocation's addend repeats.
                const int64_t value = !plain && executable_is_tls(link, symbol) ? (int32_t)reference : (int64_t)base + (int32_t)reference;
                memcpy(got + 8 * (uint64_t)slot, &value, sizeof(value));
            }
        }
    }
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
    uint8_t* stubs = shared->section_bytes[EXECUTABLE_SECTION_PLT_GOT];
    const uint64_t stubs_address = layout->sections[EXECUTABLE_SECTION_PLT_GOT].address;
    const uint64_t got_address = layout->sections[EXECUTABLE_SECTION_GOT].address;
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
                // jmp [rip + slot], from the end of its 6 bytes, and a two-byte nop.
                uint8_t* bytes = stubs + (reference - stubs_address);
                const int32_t distance = (int32_t)(got_address + 8 * (uint64_t)slot - (reference + 6));
                bytes[0] = 0xff;
                bytes[1] = 0x25;
                memcpy(bytes + 2, &distance, sizeof(distance));
                bytes[6] = 0x66;
                bytes[7] = 0x90;
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
        shared->boundaries[chunk] = executable_chunk_boundary(link, shared->pieces, layout->piece_count, chunk * BUSTER_EXECUTABLE_CHUNK_BYTES);
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
                const uint8_t class = (kind & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_ABS64 ? executable_abs64_class(link, target, outside) : BUSTER_EXECUTABLE_RELOCATION_NONE;
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
                const bool relative = abs64 && executable_abs64_class(link, target, outside) == BUSTER_EXECUTABLE_RELOCATION_RELATIVE;
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

// A dynamic PIE's sections that lane 0 writes once the symbols are resolved: the exports' .dynsym entries, names and
// versions after the undefined ones, in .gnu.hash's order (by bucket, a counting sort that keeps ExecutableDynamic's
// order within one), their indices, and .gnu.hash; the library's name and the needed versions' after the names in
// .dynstr, and .gnu.version_r; and .dynamic. Returns false if the arena can't hold the exports' hashes and order.
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

        // The library's name, then the needed versions' names, each with its NUL, and .gnu.version_r: one entry, for the
        // library, and an auxiliary entry per needed version, in ExecutableDynamic's order.
        memcpy(dynstr + totals->needed_name, dynamic->needed.pointer, dynamic->needed.length);
        dynstr[totals->needed_name + dynamic->needed.length] = 0;
        uint8_t* verneed = shared->section_bytes[EXECUTABLE_SECTION_VERNEED];
        uint64_t version_name = totals->version_names;
        uint32_t auxiliary = 0;
        if (totals->versions != 0)
        {
            const Elf64_Verneed needed = {
                .vn_version = VER_NEED_CURRENT,
                .vn_cnt = (uint16_t)totals->versions,
                .vn_file = (uint32_t)totals->needed_name,
                .vn_aux = sizeof(Elf64_Verneed),
                .vn_next = 0,
            };
            memcpy(verneed, &needed, sizeof(needed));
        }
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
                const Elf64_Vernaux entry = {
                    .vna_hash = version->hash,
                    .vna_flags = 0,
                    .vna_other = number,
                    .vna_name = (uint32_t)version_name,
                    .vna_next = auxiliary + 1 < totals->versions ? sizeof(Elf64_Vernaux) : 0,
                };
                memcpy(verneed + sizeof(Elf64_Verneed) + auxiliary * sizeof(Elf64_Vernaux), &entry, sizeof(entry));
                version_name += version->name.length + 1;
                auxiliary += 1;
            }
        }

        // .interp and the ABI note, copied, and .dynamic.
        uint8_t* interp = shared->section_bytes[EXECUTABLE_SECTION_INTERP];
        memcpy(interp, dynamic->interpreter.pointer, dynamic->interpreter.length);
        interp[dynamic->interpreter.length] = 0;
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
        // The atoms' pieces and the linker's: the headers, its sections', .shstrtab and the section headers.
        shared->pieces = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutablePiece, (uint64_t)link->atoms.count + EXECUTABLE_SECTION_COUNT + 3);
        shared->exact = options->exact;
        fits = shared->counts != 0 && shared->errors != 0 && shared->got_marks != 0 && shared->got_counts != 0 && shared->addend_magnitudes != 0 && shared->pieces != 0;
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
        shared->relr = dynamic->relr_version != 0;
        shared->dynamic_bits = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, shared->words_per_lane);
        shared->relative_bits = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, shared->words_per_lane);
        shared->filters = arena_push_array(arena, (uint64_t)lanes * BUSTER_EXECUTABLE_FILTER_WORDS, sizeof(uint32_t), 64);
        shared->relr_words = shared->relr ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, link->atoms.count) : 0;
        shared->dynamic_counts = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableDynamicCounts, lanes);
        shared->versions_used = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, lanes * shared->version_words);
        shared->version_numbers = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, limit);
        shared->stub_marks = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, lanes * shared->words_per_lane);
        fits = shared->dynamic_bits != 0 && shared->relative_bits != 0 && shared->filters != 0 &&
               (!shared->relr || shared->relr_words != 0) && shared->dynamic_counts != 0 && shared->versions_used != 0 && shared->version_numbers != 0 &&
               shared->stub_marks != 0;
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
    // The sections of the linker's own that their phases fill, a dynamic PIE's and .eh_frame_hdr: their first entries
    // here.
    for (uint32_t section = 0; result && section < EXECUTABLE_SECTION_COUNT; section += 1)
    {
        const bool own = section != EXECUTABLE_SECTION_BUILD_ID && section != EXECUTABLE_SECTION_GOT && executable_linker_before[section] != BUSTER_LINK_CLASS_COUNT &&
                         (layout->dynamic || section == EXECUTABLE_SECTION_EH_FRAME_HDR);
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
    const bool dynamic = dynamic_options != 0;
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

    // A dynamic PIE's symbols first: the bitsets and the filter that the count's tests read. Then count the atoms and
    // the GOT's targets (and a dynamic PIE's fixups), then reduce the GOT's marks (and classify a dynamic PIE's ABS64
    // fixups, and lane 0 numbers its versions). Lane 0 checks the entry point first, so that a bad one is the error
    // reported with any number of lanes.
    if (ok && dynamic)
    {
        executable_mark_symbols(link, shared, lane);
    }
    if (dynamic)
    {
        lane_sync();
    }
    const ExecutableFilter filter = ok ? executable_filter(shared, lanes) : (ExecutableFilter){ 0 };
    const SymbolId entry = options->entry;
    const AtomId entry_atom = entry != 0 && entry < link->symbols.count ? link->symbols.atom[entry] : 0;
    if (ok && lane == 0 && (entry_atom == 0 || link->atoms.class[entry_atom] != BUSTER_LINK_CLASS_TEXT))
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_COUNT, lane, lanes), EXECUTABLE_ERROR_ENTRY, 0, entry, 0);
    }
    ExecutableFrames frames = { 0 };
    if (ok)
    {
        executable_count(link, shared, lane, dynamic, &filter, executable_error(shared, EXECUTABLE_PHASE_COUNT, lane, lanes));
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
    // .eh_frame_hdr's size: an executable with .eh_frame atoms has one, with an entry per FDE.
    uint64_t fdes = 0;
    bool unwind = false;
    for (uint32_t other = 0; ok && other < lanes; other += 1)
    {
        fdes += shared->counts[other].fdes;
        unwind = unwind || shared->counts[other].bytes[executable_bucket(BUSTER_LINK_CLASS_EH_FRAME, 0)] != 0;
    }
    totals.linker_bytes[EXECUTABLE_SECTION_EH_FRAME_HDR] = unwind ? BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES + 8 * fdes : 0;
    const bool laid_out = ok && executable_lay_out(layout, shared->counts, lanes, got_slots, dynamic, totals.linker_bytes);
    if (ok && !laid_out)
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes), EXECUTABLE_ERROR_LAYOUT, 0, 0, 0);
    }
    if (laid_out)
    {
        executable_place(link, shared, layout, lane, executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes));
    }
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
    if (dynamic || indexed)
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

    // Patch, hash and write the chunks.
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
        // Every S + A - P of CALL and PC32 fits in int32 when the base, the image and the largest addend do together.
        .checked = !ok || layout->base + layout->image_size + magnitude >= BUSTER_EXECUTABLE_MAX_ADDRESS,
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
            result->relocations = totals.got_relative + totals.got_symbolic + totals.abs64_relative + totals.abs64_symbolic;
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
