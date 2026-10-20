// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The direct path's executables: a link (link.c) laid out, patched and written as a static executable at a fixed
// address, in one call that every lane of a group makes (docs/object-writer-and-linker.md, "The direct path (no -c)",
// "Layout" and "Patching and writing"). It is the first output the direct path writes (step 4 of the plan): no libc, no
// crt objects and no dynamic sections. The program's _start makes its system calls, and the linker defines
// __init_array_start, __init_array_end, __fini_array_start and __fini_array_end for it.
//
// The image, at BUSTER_EXECUTABLE_BASE, is lld's: four PT_LOAD segments in lld's order, each from the next page at its
// file offset's place in a page (4 KB-congruent, with contiguous file offsets and no file padding):
//   - R: the ELF header and program headers (which a program finds through AT_PHDR), .note.gnu.build-id, and .rodata
//     (RODATA atoms, then STRINGS atoms);
//   - RX: .text;
//   - RW, RELRO: .tdata and .tbss (the TLS image, PT_TLS), .init_array, .fini_array, .data.rel.ro and .got, its memory
//     size running to its page's end so that PT_GNU_RELRO covers it all;
//   - RW: .data and .bss.
// Then the section headers' string table and the section headers, which no segment loads. Within a section, atoms are
// ordered by alignment, largest first, and by id within an alignment, so that each one's offset is a prefix sum of the
// sizes before it with no padding: a data atom's size is padded to a multiple of its alignment, and a function's to 16
// bytes with int3 (functions are at least 16-byte aligned). The atoms of INIT_ARRAY and FINI_ARRAY, whose order the
// program runs, keep id order: they must be whole 8-byte entries, aligned to at most 8. Atoms aligned past a page
// aren't supported yet. The GOT has a slot for each target of CALL_GOT, LOAD_GOT, GOTPCREL and GOTTPOFF, numbered from
// 1 in symbol order (slot 0 stays zero, so that got_slot 0 means none): the target's address, or for GOTTPOFF its TLS
// offset. A static executable needs no dynamic relocations, and nothing is relaxed: on the direct path the code
// generator binds before it selects instructions, so the program's code reaches its own symbols directly.
//
// The columns it writes are the linker's: atoms' address (an image offset, the address less the base) and symbols'
// reference and got_slot. A symbol's reference is its image offset, or a TLS symbol's offset from the thread pointer
// (variant II: from the end of the TLS block, which is the TLS segment's memory size rounded up to its alignment, since
// the segment starts on a multiple of its alignment), or for a weak symbol defined nowhere the image offset of address
// 0, or 0 if the symbol is thread-local, as GNU ld resolves one. Every fixup's field is patched in place, in its
// atom's bytes.
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
// (lane_range: each loop is a few nanoseconds per item), with count, place and fill by lane; the chunks are cut into
// parts by bytes and taken from the counter (lane_parts_by_cost and lane_take). Lane 0 writes the headers, opens the
// output for every lane, and writes the build ID and closes it. The output is byte-identical for every lane count.
//
// main.c includes this file after lane.c, x86_64_encoder.c and link.c, and its String8. Layout, in this order:
//   - API: the base, the chunks, ExecutableSection, ExecutableError, ExecutableWrite, ExecutableOptions,
//     ExecutableResult and executable_write's prototype.
//   - The chunk hash: ExecutableHash, executable_hash_begin, executable_hash_bytes and executable_hash_end.
//   - Patching: ExecutablePatch, executable_patch_pc32, executable_patch_other and executable_patch_range.
//   - The file: executable_write_all, executable_open and executable_close.
//   - Layout: the buckets (executable_bucket), ExecutableLayout and executable_lay_out.
//   - Headers: executable_write_headers.
//   - Chunks: executable_chunk_boundary, executable_patch_chunk and executable_chunk_runs.
//   - Writing: ExecutableShared, executable_chunks and executable_write.

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

// Where the image starts: GNU ld's base for an executable that isn't a PIE.
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

// The output sections, in the order of the file and of the addresses. .tbss takes no address space in its segment:
// the section after it starts where it does.
typedef enum ExecutableSection
{
    EXECUTABLE_SECTION_BUILD_ID,
    EXECUTABLE_SECTION_RODATA,
    EXECUTABLE_SECTION_TEXT,
    EXECUTABLE_SECTION_TDATA,
    EXECUTABLE_SECTION_TBSS,
    EXECUTABLE_SECTION_INIT_ARRAY,
    EXECUTABLE_SECTION_FINI_ARRAY,
    EXECUTABLE_SECTION_DATA_RELRO,
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
    // isn't whole 8-byte entries aligned to at most 8, or an atom is of one of the C runtime's classes (EH_FRAME, INIT
    // and FINI), which an executable without libc has no section for (ExecutableResult.atom).
    EXECUTABLE_ERROR_LAYOUT,
    // A symbol defined nowhere that isn't weak (ExecutableResult.symbol).
    EXECUTABLE_ERROR_UNDEFINED,
    // A symbol of a shared library, or one the linker should define that it doesn't know (ExecutableResult.symbol).
    EXECUTABLE_ERROR_UNSUPPORTED,
    // The entry point isn't a symbol defined in a TEXT atom.
    EXECUTABLE_ERROR_ENTRY,
    // A fixup's value doesn't fit its field (ExecutableResult.fixup).
    EXECUTABLE_ERROR_FIXUP,
    // A system call on the output failed (ExecutableResult.call and system_error).
    EXECUTABLE_ERROR_FILE,
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
    // Counting the atoms per bucket, and the GOT's targets.
    EXECUTABLE_PHASE_COUNT,
    // Laying out the sections and the atoms, numbering the GOT slots, and the headers.
    EXECUTABLE_PHASE_LAYOUT,
    // The symbols' references, the GOT's slots and the chunks' boundaries.
    EXECUTABLE_PHASE_SYMBOLS,
    // Opening the output.
    EXECUTABLE_PHASE_OPEN,
    // Patching, hashing and writing the chunks.
    EXECUTABLE_PHASE_CHUNKS,
    // The build ID, and closing.
    EXECUTABLE_PHASE_FINISH,
    EXECUTABLE_PHASES,
} ExecutablePhase;

typedef struct ExecutableOptions ExecutableOptions;
struct ExecutableOptions
{
    // The output's path, which needn't be followed by a NUL.
    String8 path;
    // The entry point: a symbol defined in a TEXT atom (_start).
    SymbolId entry;
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
    // The GOT's image offset.
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
    return (int64_t)BUSTER_EXECUTABLE_BASE + (int32_t)patch->reference[symbol];
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
    const int64_t place = (int64_t)(BUSTER_EXECUTABLE_BASE + address + offset);
    const int64_t symbol = executable_address(&patch, target);
    const int64_t slot = (int64_t)(BUSTER_EXECUTABLE_BASE + patch.got) + 8 * (int64_t)patch.got_slots[target];
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
    [EXECUTABLE_SECTION_BUILD_ID] = ".note.gnu.build-id",
    [EXECUTABLE_SECTION_RODATA] = ".rodata",
    [EXECUTABLE_SECTION_TEXT] = ".text",
    [EXECUTABLE_SECTION_TDATA] = ".tdata",
    [EXECUTABLE_SECTION_TBSS] = ".tbss",
    [EXECUTABLE_SECTION_INIT_ARRAY] = ".init_array",
    [EXECUTABLE_SECTION_FINI_ARRAY] = ".fini_array",
    [EXECUTABLE_SECTION_DATA_RELRO] = ".data.rel.ro",
    [EXECUTABLE_SECTION_GOT] = ".got",
    [EXECUTABLE_SECTION_DATA] = ".data",
    [EXECUTABLE_SECTION_BSS] = ".bss",
};

static const uint32_t executable_section_types[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_BUILD_ID] = SHT_NOTE,
    [EXECUTABLE_SECTION_RODATA] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_TEXT] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_TDATA] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_TBSS] = SHT_NOBITS,
    [EXECUTABLE_SECTION_INIT_ARRAY] = SHT_INIT_ARRAY,
    [EXECUTABLE_SECTION_FINI_ARRAY] = SHT_FINI_ARRAY,
    [EXECUTABLE_SECTION_DATA_RELRO] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_GOT] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_DATA] = SHT_PROGBITS,
    [EXECUTABLE_SECTION_BSS] = SHT_NOBITS,
};

static const uint64_t executable_section_flags[EXECUTABLE_SECTION_COUNT] = {
    [EXECUTABLE_SECTION_BUILD_ID] = SHF_ALLOC,
    [EXECUTABLE_SECTION_RODATA] = SHF_ALLOC,
    [EXECUTABLE_SECTION_TEXT] = SHF_ALLOC | SHF_EXECINSTR,
    [EXECUTABLE_SECTION_TDATA] = SHF_ALLOC | SHF_WRITE | SHF_TLS,
    [EXECUTABLE_SECTION_TBSS] = SHF_ALLOC | SHF_WRITE | SHF_TLS,
    [EXECUTABLE_SECTION_INIT_ARRAY] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_FINI_ARRAY] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_DATA_RELRO] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_GOT] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_DATA] = SHF_ALLOC | SHF_WRITE,
    [EXECUTABLE_SECTION_BSS] = SHF_ALLOC | SHF_WRITE,
};

#define BUSTER_EXECUTABLE_ALIGNMENTS (BUSTER_EXECUTABLE_PAGE_LOG2 + 1)
#define BUSTER_EXECUTABLE_BUCKETS ((BUSTER_LINK_CLASS_COUNT - 1) * BUSTER_EXECUTABLE_ALIGNMENTS)
#define BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2 3

// The section each class's atoms go to, or EXECUTABLE_SECTION_COUNT for the C runtime's classes, which an executable
// without libc doesn't have (step 5's).
static const uint8_t executable_class_sections[BUSTER_LINK_CLASS_COUNT] = {
    [BUSTER_LINK_CLASS_NONE] = EXECUTABLE_SECTION_COUNT,
    [BUSTER_LINK_CLASS_RODATA] = EXECUTABLE_SECTION_RODATA,
    [BUSTER_LINK_CLASS_STRINGS] = EXECUTABLE_SECTION_RODATA,
    [BUSTER_LINK_CLASS_EH_FRAME] = EXECUTABLE_SECTION_COUNT,
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

// An atom's alignment as its bucket has it, or BUSTER_EXECUTABLE_ALIGNMENTS if it isn't one the layout takes.
static uint32_t executable_alignment_log2(const LinkAtoms* atoms, AtomId atom)
{
    const uint8_t class = atoms->class[atom];
    uint32_t result = atoms->alignment_log2[atom];
    if (class == BUSTER_LINK_CLASS_TEXT)
    {
        result = result < BUSTER_EXECUTABLE_FUNCTION_ALIGNMENT_LOG2 ? BUSTER_EXECUTABLE_FUNCTION_ALIGNMENT_LOG2 : result;
    }
    else if (class == BUSTER_LINK_CLASS_INIT_ARRAY || class == BUSTER_LINK_CLASS_FINI_ARRAY)
    {
        const bool entries = result <= BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2 && atoms->size[atom] % 8 == 0;
        result = entries ? BUSTER_EXECUTABLE_ENTRY_ALIGNMENT_LOG2 : BUSTER_EXECUTABLE_ALIGNMENTS;
    }
    else if (executable_class_sections[class] == EXECUTABLE_SECTION_COUNT)
    {
        result = BUSTER_EXECUTABLE_ALIGNMENTS;
    }
    return result > BUSTER_EXECUTABLE_PAGE_LOG2 ? BUSTER_EXECUTABLE_ALIGNMENTS : result;
}

static uint32_t executable_bucket(uint8_t class, uint32_t alignment_log2)
{
    return (uint32_t)(class - 1) * BUSTER_EXECUTABLE_ALIGNMENTS + (BUSTER_EXECUTABLE_PAGE_LOG2 - alignment_log2);
}

// Whether an atom's bytes go in the file: it has some, and isn't zero-fill.
static bool executable_in_file(const LinkAtoms* atoms, AtomId atom)
{
    return atoms->size[atom] != 0 && !link_class_is_zero_fill(atoms->class[atom]);
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

// Program headers: PT_PHDR, a PT_LOAD per segment with bytes, PT_TLS with TLS, PT_GNU_RELRO with a RELRO segment,
// PT_GNU_STACK and PT_NOTE.
#define BUSTER_EXECUTABLE_MAX_PROGRAM_HEADERS 9
// Section headers: the null one, the sections with bytes or memory, and .shstrtab.
#define BUSTER_EXECUTABLE_MAX_SECTION_HEADERS (EXECUTABLE_SECTION_COUNT + 2)
#define BUSTER_EXECUTABLE_SHSTRTAB_BYTES 160

// What every lane computes alike from the counts: where the sections, the segments and each bucket go.
typedef struct ExecutableLayout ExecutableLayout;
struct ExecutableLayout
{
    ExecutableSectionLayout sections[EXECUTABLE_SECTION_COUNT];
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
    // The pieces of the linker's own: the headers, the note, the GOT (when it has slots), .shstrtab and the section
    // headers.
    uint64_t got_piece;
    uint64_t shstrtab_offset;
    uint64_t shstrtab_size;
    uint64_t section_headers_offset;
    uint32_t section_header_count;
    uint64_t piece_count;
    uint64_t image_size;
    uint64_t file_size;
    uint64_t chunk_count;
};

// The counts of one lane's atoms: per bucket, how many, their padded bytes and how many go in the file.
typedef struct ExecutableCounts ExecutableCounts;
struct ExecutableCounts
{
    uint64_t bytes[BUSTER_EXECUTABLE_BUCKETS];
    uint64_t pieces[BUSTER_EXECUTABLE_BUCKETS];
};

// Places the sections, from every lane's counts and the GOT's slots: the buckets one after another within their
// sections, each section at its alignment, and the segments 4 KB-congruent from the next page. Returns false if the
// image reaches BUSTER_EXECUTABLE_MAX_ADDRESS or the TLS block is larger than BUSTER_EXECUTABLE_MAX_TLS_BYTES. The
// atoms are at most BUSTER_EXECUTABLE_MAX_ATOM_BYTES each, so no sum of their sizes wraps.
static bool executable_lay_out(ExecutableLayout* layout, const ExecutableCounts* counts, uint32_t lanes, uint64_t got_slots)
{
    *layout = (ExecutableLayout){ 0 };
    uint64_t bytes[BUSTER_EXECUTABLE_BUCKETS] = { 0 };
    uint64_t pieces[BUSTER_EXECUTABLE_BUCKETS] = { 0 };
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        for (uint32_t bucket = 0; bucket < BUSTER_EXECUTABLE_BUCKETS; bucket += 1)
        {
            bytes[bucket] += counts[lane].bytes[bucket];
            pieces[bucket] += counts[lane].pieces[bucket];
        }
    }
    // Each section's size and alignment, from its buckets'.
    uint64_t section_bytes[EXECUTABLE_SECTION_COUNT] = { 0 };
    uint64_t section_alignment[EXECUTABLE_SECTION_COUNT];
    for (uint32_t section = 0; section < EXECUTABLE_SECTION_COUNT; section += 1)
    {
        section_alignment[section] = 1;
    }
    section_bytes[EXECUTABLE_SECTION_BUILD_ID] = BUSTER_EXECUTABLE_NOTE_BYTES;
    section_alignment[EXECUTABLE_SECTION_BUILD_ID] = 4;
    section_bytes[EXECUTABLE_SECTION_GOT] = got_slots != 0 ? 8 * (got_slots + 1) : 0;
    section_alignment[EXECUTABLE_SECTION_GOT] = 8;
    // STRINGS follow RODATA in .rodata, at their own largest alignment.
    uint64_t strings_start = 0;
    for (uint8_t class = 1; class < BUSTER_LINK_CLASS_COUNT; class += 1)
    {
        const uint8_t section = executable_class_sections[class];
        uint64_t class_alignment = 0;
        uint64_t class_bytes = 0;
        for (uint32_t log2 = BUSTER_EXECUTABLE_PAGE_LOG2 + 1; log2-- > 0;)
        {
            const uint32_t bucket = executable_bucket(class, log2);
            class_alignment = class_alignment == 0 && bytes[bucket] != 0 ? 1ull << log2 : class_alignment;
            class_bytes += bytes[bucket];
        }
        if (class_alignment != 0)
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
    layout->has_segment[EXECUTABLE_SEGMENT_RX] = section_bytes[EXECUTABLE_SECTION_TEXT] != 0;
    layout->has_segment[EXECUTABLE_SEGMENT_RELRO] = section_bytes[EXECUTABLE_SECTION_TDATA] + section_bytes[EXECUTABLE_SECTION_INIT_ARRAY] +
                                                        section_bytes[EXECUTABLE_SECTION_FINI_ARRAY] + section_bytes[EXECUTABLE_SECTION_DATA_RELRO] +
                                                        section_bytes[EXECUTABLE_SECTION_GOT] != 0;
    layout->has_segment[EXECUTABLE_SEGMENT_RW] = section_bytes[EXECUTABLE_SECTION_DATA] + section_bytes[EXECUTABLE_SECTION_BSS] != 0;
    layout->program_headers = 4 + (has_tls ? 1 : 0) + (layout->has_segment[EXECUTABLE_SEGMENT_RELRO] ? 1 : 0);
    for (uint32_t segment = EXECUTABLE_SEGMENT_RX; segment < EXECUTABLE_SEGMENTS; segment += 1)
    {
        layout->program_headers += layout->has_segment[segment] ? 1 : 0;
    }
    layout->header_bytes = sizeof(Elf64_Ehdr) + layout->program_headers * sizeof(Elf64_Phdr);

    // The sections, each segment starting on the page after the last one's end at its offset's place in a page.
    static const uint8_t first_sections[EXECUTABLE_SEGMENTS] = {
        EXECUTABLE_SECTION_BUILD_ID,
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

    // Each bucket's start: its class's start in its section, then the buckets before it.
    uint64_t piece = 2;
    for (uint8_t class = 1; class < BUSTER_LINK_CLASS_COUNT; class += 1)
    {
        const uint8_t section = executable_class_sections[class];
        if (class == BUSTER_LINK_CLASS_DATA)
        {
            layout->got_piece = got_slots != 0 ? piece : 0;
            piece += got_slots != 0 ? 1 : 0;
        }
        uint64_t cursor = layout->sections[section].address + (class == BUSTER_LINK_CLASS_STRINGS ? strings_start : 0);
        for (uint32_t log2 = BUSTER_EXECUTABLE_PAGE_LOG2 + 1; log2-- > 0;)
        {
            const uint32_t bucket = executable_bucket(class, log2);
            cursor = bytes[bucket] != 0 ? executable_align(cursor, 1ull << log2) : cursor;
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
        layout->section_header_count += present ? 1 : 0;
        layout->shstrtab_size += present ? strlen(executable_section_names[section]) + 1 : 0;
    }
    layout->shstrtab_offset = offset;
    layout->section_headers_offset = executable_align(offset + layout->shstrtab_size, 8);
    layout->file_size = layout->section_headers_offset + layout->section_header_count * sizeof(Elf64_Shdr);
    layout->piece_count = piece + 2;
    layout->chunk_count = (layout->file_size + BUSTER_EXECUTABLE_CHUNK_BYTES - 1) / BUSTER_EXECUTABLE_CHUNK_BYTES;
    return BUSTER_EXECUTABLE_BASE + layout->image_size <= BUSTER_EXECUTABLE_MAX_ADDRESS && tls_fits;
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

static Elf64_Phdr executable_program_header(uint32_t type, uint32_t flags, uint64_t offset, uint64_t address, uint64_t file_size, uint64_t memory_size, uint64_t alignment)
{
    Elf64_Phdr result = {
        .p_type = type,
        .p_flags = flags,
        .p_offset = offset,
        .p_vaddr = BUSTER_EXECUTABLE_BASE + address,
        .p_paddr = BUSTER_EXECUTABLE_BASE + address,
        .p_filesz = file_size,
        .p_memsz = memory_size,
        .p_align = alignment,
    };
    return result;
}

// The headers, in lld's order of program headers: PT_PHDR, the PT_LOADs, PT_TLS, PT_GNU_RELRO, PT_GNU_STACK (not
// executable) and PT_NOTE. The note's hash stays zero until the build ID is known.
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
        .e_type = ET_EXEC,
        .e_machine = EM_X86_64,
        .e_version = EV_CURRENT,
        .e_entry = BUSTER_EXECUTABLE_BASE + entry,
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
    program[count] = executable_program_header(PT_PHDR, PF_R, sizeof(Elf64_Ehdr), sizeof(Elf64_Ehdr), program_bytes, program_bytes, 8);
    count += 1;
    for (uint32_t segment = 0; segment < EXECUTABLE_SEGMENTS; segment += 1)
    {
        const ExecutableSegmentLayout* placed = &layout->segments[segment];
        if (layout->has_segment[segment])
        {
            program[count] = executable_program_header(PT_LOAD, load_flags[segment], placed->offset, placed->address, placed->file_size, placed->memory_size,
                                                       BUSTER_EXECUTABLE_PAGE_BYTES);
            count += 1;
        }
    }
    const ExecutableSectionLayout* tdata = &layout->sections[EXECUTABLE_SECTION_TDATA];
    if (layout->tls_size != 0)
    {
        program[count] = executable_program_header(PT_TLS, PF_R, tdata->offset, tdata->address, tdata->size, layout->tls_size, layout->tls_alignment);
        count += 1;
    }
    if (layout->has_segment[EXECUTABLE_SEGMENT_RELRO])
    {
        const ExecutableSegmentLayout* relro = &layout->segments[EXECUTABLE_SEGMENT_RELRO];
        program[count] = executable_program_header(PT_GNU_RELRO, PF_R, relro->offset, relro->address, relro->file_size, relro->memory_size, 1);
        count += 1;
    }
    program[count] = (Elf64_Phdr){
        .p_type = PT_GNU_STACK,
        .p_flags = PF_R | PF_W,
    };
    count += 1;
    const ExecutableSectionLayout* note = &layout->sections[EXECUTABLE_SECTION_BUILD_ID];
    program[count] = executable_program_header(PT_NOTE, PF_R, note->offset, note->address, note->size, note->size, 4);
    count += 1;
    memcpy(headers->headers + sizeof(Elf64_Ehdr), program, count * sizeof(Elf64_Phdr));

    const Elf64_Nhdr note_header = {
        .n_namesz = 4,
        .n_descsz = BUSTER_EXECUTABLE_BUILD_ID_BYTES,
        .n_type = NT_GNU_BUILD_ID,
    };
    memcpy(headers->note, &note_header, sizeof(note_header));
    memcpy(headers->note + sizeof(note_header), "GNU", 4);

    // The section headers: the null one, every section with a size, and .shstrtab, whose names it holds.
    uint32_t name = 1;
    uint32_t index = 1;
    for (uint32_t section = 0; section < EXECUTABLE_SECTION_COUNT; section += 1)
    {
        const ExecutableSectionLayout* placed = &layout->sections[section];
        if (placed->size != 0)
        {
            const uint32_t length = (uint32_t)strlen(executable_section_names[section]);
            memcpy(headers->shstrtab + name, executable_section_names[section], length + 1);
            const bool entries = section == EXECUTABLE_SECTION_INIT_ARRAY || section == EXECUTABLE_SECTION_FINI_ARRAY || section == EXECUTABLE_SECTION_GOT;
            headers->section_headers[index] = (Elf64_Shdr){
                .sh_name = name,
                .sh_type = executable_section_types[section],
                .sh_flags = executable_section_flags[section],
                .sh_addr = BUSTER_EXECUTABLE_BASE + placed->address,
                .sh_offset = placed->offset,
                .sh_size = placed->size,
                .sh_link = 0,
                .sh_info = 0,
                .sh_addralign = placed->alignment,
                .sh_entsize = entries ? 8 : 0,
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

// What the lanes share, which lane 0 allocates from its scratch arena and broadcasts. Each lane writes its own row of
// the per-lane arrays, and its own range of the others.
typedef struct ExecutableShared ExecutableShared;
struct ExecutableShared
{
    ExecutableCounts* counts;
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

// A symbol's reference (see the orientation comment), or the error it gets.
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
    else if (source == BUSTER_LINK_SOURCE_NONE && weak)
    {
        // Address 0, as an image offset; for a thread-local one, an offset of 0 from the thread pointer, in TPOFF32
        // fields and GOT slots alike, as GNU ld gives it.
        *reference = executable_is_tls(link, symbol) ? 0 : (uint32_t)(0 - BUSTER_EXECUTABLE_BASE);
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

// Counts this lane's atoms per bucket, and marks in its bitset the targets of the GOT kinds among its fixups, whose
// largest addend magnitude it records. Failures go to `error`.
static void executable_count(Link* link, ExecutableShared* shared, uint32_t lane, ExecutableLaneError* error)
{
    const LinkAtoms* atoms = &link->atoms;
    ExecutableCounts* counts = &shared->counts[lane];
    memset(counts, 0, sizeof(*counts));
    const Range64 atom_range = lane_range(atoms->count - 1);
    for (AtomId atom = (AtomId)atom_range.begin + 1; atom < atom_range.end + 1; atom += 1)
    {
        const uint32_t log2 = executable_alignment_log2(atoms, atom);
        // Bounded before its padding is added, which could wrap: up to 2^32 atoms of 2 GB each sum to at most 2^63.
        if (log2 == BUSTER_EXECUTABLE_ALIGNMENTS || atoms->size[atom] > BUSTER_EXECUTABLE_MAX_ATOM_BYTES)
        {
            executable_lane_fail(error, EXECUTABLE_ERROR_LAYOUT, atom, 0, 0);
        }
        else
        {
            const uint32_t bucket = executable_bucket(atoms->class[atom], log2);
            counts->bytes[bucket] += executable_align(atoms->size[atom], 1ull << log2);
            counts->pieces[bucket] += executable_in_file(atoms, atom) ? 1 : 0;
        }
    }

    const LinkFixups* fixups = &link->fixups;
    uint64_t* marks = shared->got_marks + lane * shared->words_per_lane;
    memset(marks, 0, shared->words_per_lane * sizeof(*marks));
    // 64 fixups at a time: a compare of their kinds finds the GOT kinds' (CALL_GOT, LOAD_GOT and GOTPCREL are 2 to 4,
    // and GOTTPOFF), whose targets get their bits one at a time, and the addend column's magnitudes go into a maximum.
    // Masked loads stay within the columns.
    const Range64 fixup_range = lane_range(fixups->count - 1);
    const FixupId end = (FixupId)fixup_range.end + 1;
    __m512i largest = _mm512_setzero_si512();
    for (FixupId group = (FixupId)fixup_range.begin + 1; group < end; group += 64)
    {
        const uint64_t count = end - group;
        const __mmask64 valid = count >= 64 ? ~0ull : (1ull << count) - 1;
        const __m512i kinds = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, fixups->kind + group), _mm512_set1_epi8(BUSTER_LINK_FIXUP_KIND));
        const __mmask64 slot_reads = _mm512_cmple_epu8_mask(_mm512_sub_epi8(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_CALL_GOT)), _mm512_set1_epi8(2));
        uint64_t got = valid & (slot_reads | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_LINK_FIXUP_GOTTPOFF)));
        while (got != 0)
        {
            const SymbolId target = fixups->target[group + (FixupId)__builtin_ctzll(got)];
            marks[target / 64] |= 1ull << (target % 64);
            got &= got - 1;
        }
        for (uint32_t part = 0; part < 4; part += 1)
        {
            const __mmask16 lanes = (__mmask16)(valid >> (16 * part));
            const __m512i addends = _mm512_maskz_loadu_epi32(lanes, fixups->addend + group + 16 * part);
            largest = _mm512_max_epu32(largest, _mm512_abs_epi32(addends));
        }
    }
    // INT32_MIN's absolute value reads as 2^31, unsigned.
    shared->addend_magnitudes[lane] = _mm512_reduce_max_epu32(largest);
}

// ORs every lane's bitset into lane 0's, over this lane's range of words, and counts the slots there.
static void executable_reduce_got(ExecutableShared* shared, uint32_t lane, uint32_t lanes)
{
    const Range64 words = lane_range(shared->words_per_lane);
    uint64_t count = 0;
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        uint64_t marks = 0;
        for (uint32_t other = 0; other < lanes; other += 1)
        {
            marks |= shared->got_marks[other * shared->words_per_lane + word];
        }
        shared->got_marks[word] = marks;
        count += (uint64_t)__builtin_popcountll(marks);
    }
    shared->got_counts[lane] = count;
}

// Fills this lane's atoms' addresses and pieces, and its range of the symbols' GOT slots, from where the layout puts
// its buckets and its slots: after the lanes' before it.
static void executable_place(Link* link, ExecutableShared* shared, const ExecutableLayout* layout, uint32_t lane)
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
    LinkAtoms* atoms = &link->atoms;
    const Range64 atom_range = lane_range(atoms->count - 1);
    for (AtomId atom = (AtomId)atom_range.begin + 1; atom < atom_range.end + 1; atom += 1)
    {
        const uint8_t class = atoms->class[atom];
        const uint32_t log2 = executable_alignment_log2(atoms, atom);
        const uint32_t bucket = executable_bucket(class, log2);
        atoms->address[atom] = addresses[bucket];
        addresses[bucket] += executable_align(atoms->size[atom], 1ull << log2);
        if (executable_in_file(atoms, atom))
        {
            const uint8_t section = executable_class_sections[class];
            shared->pieces[pieces[bucket]] = (ExecutablePiece){
                .offset = executable_file_offset(layout, section, atoms->address[atom]),
                .size = atoms->size[atom],
                .bytes = atoms->bytes[atom],
                .atom = atom,
            };
            pieces[bucket] += 1;
        }
    }

    uint32_t slot = 1;
    for (uint32_t other = 0; other < lane; other += 1)
    {
        slot += (uint32_t)shared->got_counts[other];
    }
    const Range64 words = lane_range(shared->words_per_lane);
    for (uint64_t word = words.begin; word < words.end; word += 1)
    {
        for (uint32_t bit = 0; bit < 64 && word * 64 + bit < link->symbols.count; bit += 1)
        {
            const bool marked = (shared->got_marks[word] >> bit & 1) != 0;
            link->symbols.got_slot[word * 64 + bit] = marked ? slot : 0;
            slot += marked ? 1 : 0;
        }
    }
}

// Fills this lane's range of the symbols' references and their GOT slots, and of the chunks' boundaries. Failures go to
// `error`.
static void executable_resolve(Link* link, ExecutableShared* shared, const ExecutableLayout* layout, ExecutableLaneError* error)
{
    LinkSymbols* symbols = &link->symbols;
    const Range64 symbol_range = lane_range(symbols->count - 1);
    for (SymbolId symbol = (SymbolId)symbol_range.begin + 1; symbol < symbol_range.end + 1; symbol += 1)
    {
        uint32_t reference = 0;
        const ExecutableError failure = executable_reference(link, layout, symbol, &reference);
        if (failure != EXECUTABLE_ERROR_NONE)
        {
            executable_lane_fail(error, failure, 0, symbol, 0);
        }
        symbols->reference[symbol] = reference;
        const uint32_t slot = symbols->got_slot[symbol];
        if (slot != 0)
        {
            // A TLS symbol's slot holds its offset from the thread pointer (TPOFF64), another's its address.
            const int64_t value = executable_is_tls(link, symbol) ? (int32_t)reference : (int64_t)BUSTER_EXECUTABLE_BASE + (int32_t)reference;
            memcpy(shared->got + 8 * (uint64_t)slot, &value, sizeof(value));
        }
    }
    const Range64 boundary_range = lane_range(layout->chunk_count - 1);
    for (uint64_t chunk = boundary_range.begin + 1; chunk < boundary_range.end + 1; chunk += 1)
    {
        shared->boundaries[chunk] = executable_chunk_boundary(link, shared->pieces, layout->piece_count, chunk * BUSTER_EXECUTABLE_CHUNK_BYTES);
    }
}

// Allocates what the lanes share, from lane 0's scratch arena: 0 if it doesn't fit.
static ExecutableShared* executable_allocate(Arena* arena, const Link* link, uint32_t lanes)
{
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
        // The atoms' pieces and the linker's: the headers, the note, the GOT, .shstrtab and the section headers.
        shared->pieces = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutablePiece, (uint64_t)link->atoms.count + 5);
        fits = shared->counts != 0 && shared->errors != 0 && shared->got_marks != 0 && shared->got_counts != 0 && shared->addend_magnitudes != 0 && shared->pieces != 0;
    }
    if (fits)
    {
        memset(shared->errors, 0, (uint64_t)lanes * EXECUTABLE_PHASES * sizeof(ExecutableLaneError));
    }
    return fits ? shared : 0;
}

// Lane 0's part once the layout is known: the arrays sized by it, the headers, and the linker's pieces. Returns false
// if the arena can't hold them.
static bool executable_prepare(Arena* arena, ExecutableShared* shared, const ExecutableLayout* layout, uint32_t lanes)
{
    const uint64_t parts = BUSTER_LANE_PARTS_PER_LANE * (uint64_t)lanes;
    shared->got = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, 8 * (layout->got_slots + 1));
    shared->boundaries = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, layout->chunk_count + 1);
    shared->hashes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_EXECUTABLE_BUILD_ID_BYTES * layout->chunk_count);
    shared->part_boundaries = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, parts + 1);
    shared->orders = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, parts * lanes);
    const bool result = shared->got != 0 && shared->boundaries != 0 && shared->hashes != 0 && shared->part_boundaries != 0 && shared->orders != 0;
    if (result)
    {
        memset(shared->got, 0, 8 * (layout->got_slots + 1));
        shared->boundaries[0] = 0;
        shared->boundaries[layout->chunk_count] = layout->file_size;
        ExecutablePiece* pieces = shared->pieces;
        const ExecutableSectionLayout* note = &layout->sections[EXECUTABLE_SECTION_BUILD_ID];
        const ExecutableSectionLayout* got = &layout->sections[EXECUTABLE_SECTION_GOT];
        pieces[0] = (ExecutablePiece){
            .offset = 0,
            .size = layout->header_bytes,
            .bytes = shared->headers.headers,
        };
        pieces[1] = (ExecutablePiece){
            .offset = note->offset,
            .size = note->size,
            .bytes = shared->headers.note,
        };
        if (layout->got_slots != 0)
        {
            pieces[layout->got_piece] = (ExecutablePiece){
                .offset = got->offset,
                .size = got->size,
                .bytes = shared->got,
            };
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

static bool executable_write(Link* link, const ExecutableOptions* options, ExecutableResult* result)
{
    const uint32_t lane = lane_index();
    const uint32_t lanes = lane_count();
    Scratch scratch = scratch_begin(0);
    ExecutableShared* allocated = 0;
    if (lane == 0)
    {
        *result = (ExecutableResult){ 0 };
        result->start_time = executable_now();
        allocated = executable_allocate(scratch.arena, link, lanes);
    }
    ExecutableShared* shared = (ExecutableShared*)lane_broadcast((uint64_t)allocated, 0);
    bool ok = shared != 0;

    // Count the atoms and the GOT's targets, then reduce the GOT's marks. Lane 0 checks the entry point first, so that
    // a bad one is the error reported with any number of lanes.
    const SymbolId entry = options->entry;
    const AtomId entry_atom = entry != 0 && entry < link->symbols.count ? link->symbols.atom[entry] : 0;
    if (ok && lane == 0 && (entry_atom == 0 || link->atoms.class[entry_atom] != BUSTER_LINK_CLASS_TEXT))
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_COUNT, lane, lanes), EXECUTABLE_ERROR_ENTRY, 0, entry, 0);
    }
    if (ok)
    {
        executable_count(link, shared, lane, executable_error(shared, EXECUTABLE_PHASE_COUNT, lane, lanes));
    }
    lane_sync();
    ok = ok && !executable_any_failed(shared, EXECUTABLE_PHASE_COUNT, lanes);
    if (ok)
    {
        executable_reduce_got(shared, lane, lanes);
    }
    lane_sync();
    if (lane == 0)
    {
        result->phase_times[EXECUTABLE_PHASE_COUNT] = executable_now();
    }

    // Lay out (every lane alike), place each lane's atoms and slots, and have lane 0 allocate what the layout sizes. A
    // lane without room for the layout fails in its error slot, so that the sync below stops every lane.
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
    const bool laid_out = ok && executable_lay_out(layout, shared->counts, lanes, got_slots);
    if (ok && !laid_out)
    {
        executable_lane_fail(executable_error(shared, EXECUTABLE_PHASE_LAYOUT, lane, lanes), EXECUTABLE_ERROR_LAYOUT, 0, 0, 0);
    }
    if (laid_out)
    {
        executable_place(link, shared, layout, lane);
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

    // The symbols' references, the GOT, the chunks' boundaries, and lane 0 the headers.
    if (ok)
    {
        executable_resolve(link, shared, layout, executable_error(shared, EXECUTABLE_PHASE_SYMBOLS, lane, lanes));
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

    // Lane 0 opens the output, once nothing can fail but a fixup's value or the file.
    ExecutableFile* file = 0;
    bool opened = false;
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
        .got = ok ? layout->sections[EXECUTABLE_SECTION_GOT].address : 0,
        // Every S + A - P of CALL and PC32 fits in int32 when the base, the image and the largest addend do together.
        .checked = !ok || BUSTER_EXECUTABLE_BASE + layout->image_size + magnitude >= BUSTER_EXECUTABLE_MAX_ADDRESS,
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
        }
        result->phase_times[EXECUTABLE_PHASE_FINISH] = executable_now();
    }
    succeeded = lane_broadcast(succeeded, 0) != 0;
    scratch_end(scratch);
    return succeeded;
}
