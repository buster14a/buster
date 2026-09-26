// Function-granular reuse of native code generation: canonical dependency
// records, the reusable artifact, the per-translation-unit pack and the
// session that ties them to one compilation. incremental.h states the
// contract and docs/incremental-compilation.md the evidence behind it.
//
// Soundness rests on one comparison: a function's artifact is replayed only
// when the record built from the current compilation is byte-identical to the
// record stored beside that artifact, under an identical context record. The
// 64-bit fingerprints order and index the pack; they never decide reuse.
//
// A record serializes every well-defined field of the prepared IrFunction
// except source provenance, then the complete closure of types it references
// and the attributes of every symbol it references. Program-level ids are
// replaced by their rank among the function's own referenced ids, which keeps
// every equality and ordering relation between them while an unrelated edit
// shifts the absolute numbers. Names of types, fields, symbols and locals are
// not serialized: native code generation does not read them (the object
// writer and debug emitters, which do, always run on the current program).
//
// Layout, in file order; each anchor is a definition to search for:
//   IncrementalWriter, incremental_writer_*     contiguous little-endian sink
//                                               in the session arena
//   IncrementalReader, incremental_reader_*     bounds-checked decoding
//   incremental_sort_indices                    iterative heapsort for the
//                                               entry index and manifest
//   incremental_compiler_identity               the running image's bytes
//   incremental_context_record                  every module-wide fact codegen
//                                               reads for a function
//   incremental_collect_symbols,                referenced symbols and the
//   incremental_collect_types                   type closure, ranked by id
//   incremental_record_function                 one function's record
//   incremental_artifact_encode/decode          the reusable artifact
//   incremental_pack_decode, incremental_pack_encode  the container
//   incremental_session_*                       the public lifecycle

#include <buster/lib/compiler/incremental/incremental.h>
#include <buster/lib/compiler/incremental/incremental_internal.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>
#include <buster/lib/time.h>

// Record and artifact growth granularity. A record for the largest function
// of the self-host unit is a few hundred kilobytes, so one chunk covers most.
#define INCREMENTAL_WRITER_CHUNK BUSTER_KB(256)
#define INCREMENTAL_SESSION_RESERVATION BUSTER_GB(64)
#define INCREMENTAL_PACK_HEADER_SIZE 64u
#define INCREMENTAL_PACK_ENTRY_SIZE 32u
#define INCREMENTAL_PACK_MANIFEST_SIZE 48u
#define INCREMENTAL_RECORD_MAGIC 0x52494942u  // "BIIR"
#define INCREMENTAL_CONTEXT_MAGIC 0x58434942u // "BICX"
#define INCREMENTAL_ARTIFACT_MAGIC 0x41434942u // "BICA"
// Decoding bounds. Each is far above anything code generation produces for
// one function and exists so that a hostile count cannot demand an
// allocation the file does not back.
#define INCREMENTAL_MAX_CODE_BYTES (1u << 30)
#define INCREMENTAL_MAX_DEBUG_PIECES 64u

BUSTER_CT_CHECK(INCREMENTAL_STATISTIC_COUNT < 256);

static u8 const incremental_pack_magic[8] = {'B', 'U', 'S', 'T', 'I', 'N', 'C', '1'};

typedef struct IncrementalWriter IncrementalWriter;
struct IncrementalWriter
{
    Arena* arena;
    u8* bytes;
    u64 count;
    u64 capacity;
    bool failed;
    u8 reserved[7];
};

typedef struct IncrementalReader IncrementalReader;
struct IncrementalReader
{
    u8 const* bytes;
    u64 length;
    u64 cursor;
    bool failed;
    u8 reserved[7];
};

typedef struct IncrementalFunctionState IncrementalFunctionState;
struct IncrementalFunctionState
{
    ByteSlice record;
    ByteSlice stored_artifact;
    ByteSlice captured_artifact;
    IrSymbolId* symbol_slots;
    IncrementalFunctionArtifact reused;
    u64 fingerprint;
    u64 sections[INCREMENTAL_SECTION_COUNT];
    u32 symbol_slot_count;
    u32 code_bytes;
#if BUSTER_INCREMENTAL_AUDIT
    u32* audit_types;
    u32 audit_type_count;
#endif
    IncrementalLookup lookup;
    IncrementalCapture capture;
    bool verify_mismatch;
    bool audit_violation;
    u8 reserved[6];
};

struct IncrementalCodegenSession
{
    Arena* arena;
    IncrementalSessionOptions options;
    String8 pack_path;
    // The previous pack stays mapped until publication has copied what it
    // reuses; decoded artifacts and republished entries point into it.
    FileMapRead previous_file;
    IncrementalPack previous;
    ByteSlice context;
    IrProgram* program;
    IrModule* module;
    IncrementalFunctionState* functions;
    u32* symbol_stamps;
    u32* symbol_ranks;
    u32* symbol_list;
    u32* type_stamps;
    u32* type_ranks;
    u32* type_list;
    IncrementalStatistics statistics;
    u32 symbol_capacity;
    u32 type_capacity;
    u32 generation;
    u32 function_count;
    u32 attempt_count;
    bool previous_loaded;
    bool bound;
    bool active;
    u8 reserved[1];
#if BUSTER_INCREMENTAL_AUDIT
    IrAccessAudit audit;
#endif
};

// ---------------------------------------------------------------------------
// Writer: a byte sink that grows by contiguous bump allocation. Only the
// writer allocates from its arena while it is open, so consecutive chunks
// are adjacent; an interleaved allocation would break adjacency, which the
// writer detects and reports as failure rather than producing a split record.

BUSTER_GLOBAL_LOCAL IncrementalWriter incremental_writer_begin(Arena* arena)
{
    return (IncrementalWriter){.arena = arena};
}

BUSTER_GLOBAL_LOCAL u8* incremental_writer_reserve(IncrementalWriter* writer, u64 size)
{
    u8* result = 0;
    if (!writer->failed && size > writer->capacity - writer->count)
    {
        u64 grow = size > INCREMENTAL_WRITER_CHUNK ? size : INCREMENTAL_WRITER_CHUNK;
        u8* chunk = (u8*)arena_allocate_bytes(writer->arena, grow, 1);
        if (!writer->bytes)
        {
            writer->bytes = chunk;
        }
        else if (chunk != writer->bytes + writer->capacity)
        {
            writer->failed = true;
        }
        writer->capacity += grow;
    }
    if (!writer->failed)
    {
        result = writer->bytes + writer->count;
        writer->count += size;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice incremental_writer_finish(IncrementalWriter* writer)
{
    ByteSlice result = {0};
    if (!writer->failed && writer->bytes)
    {
        result = (ByteSlice){.pointer = writer->bytes, .length = writer->count};
        arena_set_position(writer->arena, (u64)(writer->bytes - (u8*)writer->arena) + writer->count);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE void incremental_store_u32(u8* pointer, u32 value)
{
    pointer[0] = (u8)value;
    pointer[1] = (u8)(value >> 8);
    pointer[2] = (u8)(value >> 16);
    pointer[3] = (u8)(value >> 24);
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE void incremental_store_u64(u8* pointer, u64 value)
{
    incremental_store_u32(pointer, (u32)value);
    incremental_store_u32(pointer + 4, (u32)(value >> 32));
}

BUSTER_GLOBAL_LOCAL void incremental_write_u8(IncrementalWriter* writer, u8 value)
{
    u8* pointer = incremental_writer_reserve(writer, 1);
    if (pointer)
    {
        pointer[0] = value;
    }
}

BUSTER_GLOBAL_LOCAL void incremental_write_bytes(IncrementalWriter* writer, void const* source, u64 size)
{
    u8* pointer = size ? incremental_writer_reserve(writer, size) : 0;
    if (pointer)
    {
        memcpy(pointer, source, size);
    }
}

// Records and artifacts use unsigned LEB128: the ids, counts and offsets they
// carry are small, so most fields take one byte. The encoding of a value is
// unique, which is what keeps byte equality of records equal to equality of
// their contents.
#define INCREMENTAL_VARINT_MAX_BYTES 10u

BUSTER_GLOBAL_LOCAL void incremental_write_varint(IncrementalWriter* writer, u64 value)
{
    u8* pointer = incremental_writer_reserve(writer, INCREMENTAL_VARINT_MAX_BYTES);
    if (pointer)
    {
        u32 length = 0;
        bool more = true;
        while (more)
        {
            u8 byte = (u8)(value & 0x7f);
            value >>= 7;
            more = value != 0;
            pointer[length] = (u8)(byte | (more ? 0x80 : 0));
            length += 1;
        }
        writer->count -= INCREMENTAL_VARINT_MAX_BYTES - length;
    }
}

// An id with IR_ID_UNDERLYING_INVALID mapped to zero, so an absent id costs
// one byte.
BUSTER_GLOBAL_LOCAL void incremental_write_id(IncrementalWriter* writer, u32 id)
{
    incremental_write_varint(writer, (u32)(id + 1u));
}

BUSTER_GLOBAL_LOCAL void incremental_write_signed(IncrementalWriter* writer, s64 value)
{
    incremental_write_varint(writer, ((u64)value << 1) ^ (u64)(value >> 63));
}

BUSTER_GLOBAL_LOCAL void incremental_write_string(IncrementalWriter* writer, String8 string)
{
    incremental_write_varint(writer, string.length);
    incremental_write_bytes(writer, string.pointer, string.length);
}

// ---------------------------------------------------------------------------
// Reader: every read is checked against the remaining length; the first
// failure latches and every later read returns zero.

BUSTER_GLOBAL_LOCAL u8 const* incremental_reader_take(IncrementalReader* reader, u64 size)
{
    u8 const* result = 0;
    if (!reader->failed && size <= reader->length - reader->cursor)
    {
        result = reader->bytes + reader->cursor;
        reader->cursor += size;
    }
    else
    {
        reader->failed = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 incremental_load_u32(u8 const* pointer)
{
    return (u32)pointer[0] | ((u32)pointer[1] << 8) | ((u32)pointer[2] << 16) | ((u32)pointer[3] << 24);
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u64 incremental_load_u64(u8 const* pointer)
{
    return (u64)incremental_load_u32(pointer) | ((u64)incremental_load_u32(pointer + 4) << 32);
}

BUSTER_GLOBAL_LOCAL u8 incremental_read_u8(IncrementalReader* reader)
{
    u8 const* pointer = incremental_reader_take(reader, 1);
    return pointer ? pointer[0] : 0;
}

BUSTER_GLOBAL_LOCAL u64 incremental_read_varint(IncrementalReader* reader)
{
    u64 result = 0;
    u32 shift = 0;
    bool more = true;
    while (more && !reader->failed)
    {
        u8 byte = incremental_read_u8(reader);
        // The tenth byte may carry only the top bit of a u64, and a
        // continuation past it would describe no value; both are refused so
        // a decoded value never silently drops bits.
        if (shift == 63 && (byte & 0xfe))
        {
            reader->failed = true;
        }
        result |= (u64)(byte & 0x7f) << shift;
        more = (byte & 0x80) != 0;
        shift += 7;
        if (more && shift > 63)
        {
            reader->failed = true;
        }
    }
    return reader->failed ? 0 : result;
}

// A varint that must fit a u32 field.
BUSTER_GLOBAL_LOCAL u32 incremental_read_u32_varint(IncrementalReader* reader)
{
    u64 value = incremental_read_varint(reader);
    if (value > UINT32_MAX)
    {
        reader->failed = true;
        value = 0;
    }
    return (u32)value;
}

BUSTER_GLOBAL_LOCAL s64 incremental_read_signed(IncrementalReader* reader)
{
    u64 value = incremental_read_varint(reader);
    return (s64)((value >> 1) ^ (0 - (value & 1)));
}

BUSTER_GLOBAL_LOCAL u64 incremental_fingerprint(ByteSlice bytes)
{
    return buster_hash_64(bytes.pointer, bytes.length);
}

// Lexicographic byte order, shorter first on a common prefix.
BUSTER_GLOBAL_LOCAL s32 incremental_compare_bytes(void const* left, u64 left_length, void const* right, u64 right_length)
{
    u64 common = left_length < right_length ? left_length : right_length;
    s32 result = common ? memcmp(left, right, common) : 0;
    if (!result)
    {
        result = left_length < right_length ? -1 : left_length > right_length ? 1 : 0;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Ordering. The pack index sorts entries by (fingerprint, record bytes) and
// the manifest by name. Both are an in-place heapsort of an index array:
// iterative, O(n log n), and independent of input order.

typedef enum IncrementalSortKind
{
    INCREMENTAL_SORT_U32,
    INCREMENTAL_SORT_ENTRIES,
    INCREMENTAL_SORT_MANIFEST,
} IncrementalSortKind;

typedef struct IncrementalSortInput IncrementalSortInput;
struct IncrementalSortInput
{
    IncrementalPackEntry const* entries;
    IncrementalManifestEntry const* manifest;
    IncrementalSortKind kind;
    u8 reserved[4];
};

BUSTER_GLOBAL_LOCAL s32 incremental_entry_compare(IncrementalPackEntry const* left, IncrementalPackEntry const* right)
{
    s32 result = left->fingerprint < right->fingerprint ? -1 : left->fingerprint > right->fingerprint ? 1 : 0;
    if (!result)
    {
        result = incremental_compare_bytes(left->record.pointer, left->record.length, right->record.pointer, right->record.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s32 incremental_sort_compare(IncrementalSortInput const* input, u32 left, u32 right)
{
    s32 result;
    switch (input->kind)
    {
        break; case INCREMENTAL_SORT_U32: result = left < right ? -1 : left > right ? 1 : 0;
        break; case INCREMENTAL_SORT_ENTRIES: result = incremental_entry_compare(input->entries + left, input->entries + right);
        break; case INCREMENTAL_SORT_MANIFEST:
        {
            String8 a = input->manifest[left].name;
            String8 b = input->manifest[right].name;
            result = incremental_compare_bytes(a.pointer, a.length, b.pointer, b.length);
        }
        break; default: BUSTER_TODO();
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void incremental_sort_sift(IncrementalSortInput const* input, u32* values, u32 root, u32 count)
{
    bool done = false;
    while (!done)
    {
        u32 child = root * 2 + 1;
        if (child >= count)
        {
            done = true;
        }
        else
        {
            if (child + 1 < count && incremental_sort_compare(input, values[child], values[child + 1]) < 0)
            {
                child += 1;
            }
            if (incremental_sort_compare(input, values[root], values[child]) < 0)
            {
                u32 swap = values[root];
                values[root] = values[child];
                values[child] = swap;
                root = child;
            }
            else
            {
                done = true;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void incremental_sort_indices(IncrementalSortInput const* input, u32* values, u32 count)
{
    for (u32 index = count / 2; index != 0; index -= 1)
    {
        incremental_sort_sift(input, values, index - 1, count);
    }
    for (u32 end = count; end > 1; end -= 1)
    {
        u32 swap = values[0];
        values[0] = values[end - 1];
        values[end - 1] = swap;
        incremental_sort_sift(input, values, 0, end - 1);
    }
}

// ---------------------------------------------------------------------------
// Identity of the running compiler: the size and fingerprint of the image the
// process was loaded from. Reading it is a fixed per-invocation cost the
// statistics report separately.

IncrementalCompilerIdentity incremental_compiler_identity(Arena* arena)
{
    TimeDataType start = timestamp_take();
    IncrementalCompilerIdentity result = {0};
    TemporalArena scratch = scratch_begin(&arena, 1);
    String8 path = os_executable_path(scratch.arena);
    if (path.length)
    {
        FileMapRead image = file_map_read(scratch.arena, path, (FileReadOptions){0});
        if (image.bytes.pointer && image.bytes.length)
        {
            result.image_size = image.bytes.length;
            result.image_fingerprint = buster_hash_64(image.bytes.pointer, image.bytes.length);
            result.identified = true;
        }
        file_map_unmap(image);
    }
    scratch_end(scratch);
    result.nanoseconds = timestamp_ns_between(start, timestamp_take());
    return result;
}

String8 incremental_pack_status_string(IncrementalPackStatus status)
{
    String8 result;
    switch (status)
    {
        break; case INCREMENTAL_PACK_ABSENT: result = S8("absent");
        break; case INCREMENTAL_PACK_LOADED: result = S8("loaded");
        break; case INCREMENTAL_PACK_CORRUPT: result = S8("corrupt");
        break; case INCREMENTAL_PACK_CONTEXT_CHANGED: result = S8("context-changed");
        break; case INCREMENTAL_PACK_UNAVAILABLE: result = S8("unavailable");
        break; default: result = S8("invalid");
    }
    return result;
}

String8 incremental_lookup_string(IncrementalLookup lookup)
{
    String8 result;
    switch (lookup)
    {
        break; case INCREMENTAL_LOOKUP_NOT_LOWERED: result = S8("not-lowered");
        break; case INCREMENTAL_LOOKUP_REUSED: result = S8("reused");
        break; case INCREMENTAL_LOOKUP_MISS_NO_PACK: result = S8("miss-no-pack");
        break; case INCREMENTAL_LOOKUP_MISS_CONTEXT: result = S8("miss-context");
        break; case INCREMENTAL_LOOKUP_MISS_NEW: result = S8("miss-new");
        break; case INCREMENTAL_LOOKUP_MISS_BODY: result = S8("miss-body");
        break; case INCREMENTAL_LOOKUP_MISS_TYPES: result = S8("miss-types");
        break; case INCREMENTAL_LOOKUP_MISS_SYMBOLS: result = S8("miss-symbols");
        break; case INCREMENTAL_LOOKUP_MISS_UNCAPTURED: result = S8("miss-uncaptured");
        break; case INCREMENTAL_LOOKUP_MISS_MALFORMED: result = S8("miss-malformed");
        break; case INCREMENTAL_LOOKUP_INELIGIBLE_INLINE_ASSEMBLY: result = S8("ineligible-inline-assembly");
        break; case INCREMENTAL_LOOKUP_INELIGIBLE_RECORD: result = S8("ineligible-record");
        break; default: result = S8("invalid");
    }
    return result;
}

String8 incremental_capture_string(IncrementalCapture capture)
{
    String8 result;
    switch (capture)
    {
        break; case INCREMENTAL_CAPTURE_NONE: result = S8("none");
        break; case INCREMENTAL_CAPTURE_STORED: result = S8("stored");
        break; case INCREMENTAL_CAPTURE_CANONICAL: result = S8("canonical");
        break; case INCREMENTAL_CAPTURE_FOREIGN_SYMBOL: result = S8("foreign-symbol");
        break; case INCREMENTAL_CAPTURE_UNSUPPORTED: result = S8("unsupported");
        break; default: result = S8("invalid");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Context record: every module-wide input to one function's machine code
// generation that is not reached through the function's own ids. A pack whose
// context differs byte-for-byte is not consulted.

BUSTER_GLOBAL_LOCAL void incremental_write_type_layout(IncrementalWriter* writer, TargetTypeLayout layout)
{
    incremental_write_varint(writer, layout.size);
    incremental_write_varint(writer, layout.alignment);
    incremental_write_varint(writer, layout.bit_width);
}

BUSTER_GLOBAL_LOCAL ByteSlice incremental_context_record(Arena* arena, IncrementalCompilerIdentity compiler, IrProgram* program, Target target,
                                                         CodegenModuleOptions options, CodegenAbi abi, bool position_independent)
{
    IncrementalWriter writer = incremental_writer_begin(arena);
    incremental_write_varint(&writer, INCREMENTAL_CONTEXT_MAGIC);
    incremental_write_varint(&writer, INCREMENTAL_FORMAT_VERSION);
    incremental_write_varint(&writer, INCREMENTAL_KEY_SCHEMA);
    incremental_write_varint(&writer, compiler.image_size);
    incremental_write_varint(&writer, compiler.image_fingerprint);
    incremental_write_varint(&writer, (u64)target.cpu_arch);
    incremental_write_varint(&writer, (u64)target.cpu_model);
    incremental_write_varint(&writer, (u64)target.os);
    incremental_write_u8(&writer, target.cpu_features_explicit);
    incremental_write_varint(&writer, target.os_version_major);
    incremental_write_varint(&writer, target.os_version_minor);
    incremental_write_varint(&writer, target.os_version_patch);
    incremental_write_varint(&writer, TARGET_CPU_FEATURE_WORD_COUNT);
    for (u32 word = 0; word < TARGET_CPU_FEATURE_WORD_COUNT; word += 1)
    {
        incremental_write_varint(&writer, target.cpu_features.words[word]);
    }
    TargetDataLayout layout = program->data_layout;
    TargetTypeLayout layouts[] = {
        layout.boolean, layout.plain_char, layout.signed_char, layout.unsigned_char, layout.short_integer,
        layout.unsigned_short_integer, layout.integer, layout.unsigned_integer, layout.long_integer, layout.unsigned_long_integer,
        layout.long_long_integer, layout.unsigned_long_long_integer, layout.integer128, layout.unsigned_integer128, layout.float16_type,
        layout.bfloat16_type, layout.float_type, layout.double_type, layout.long_double_type, layout.pointer, layout.va_list,
    };
    incremental_write_varint(&writer, BUSTER_ARRAY_LENGTH(layouts));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(layouts); index += 1)
    {
        incremental_write_type_layout(&writer, layouts[index]);
    }
    incremental_write_varint(&writer, layout.atomic_min_width);
    incremental_write_varint(&writer, layout.atomic_max_width);
    incremental_write_varint(&writer, layout.atomic_alignment);
    incremental_write_varint(&writer, layout.abi_stack_alignment);
    incremental_write_varint(&writer, layout.abi_max_alignment);
    incremental_write_varint(&writer, (u64)layout.endianness);
    incremental_write_u8(&writer, layout.plain_char_is_signed);
    incremental_write_u8(&writer, layout.has_128_bit_integer);
    incremental_write_varint(&writer, (u64)abi);
    incremental_write_varint(&writer, (u64)object_format_for_target(target));
    incremental_write_u8(&writer, options.debug_info);
    incremental_write_u8(&writer, position_independent);
    incremental_write_u8(&writer, options.register_allocator);
    incremental_write_u8(&writer, options.assembly_syntax);
    incremental_write_u8(&writer, program->disable_local_promotion);
    incremental_write_u8(&writer, program->disable_target_local_promotion);
    incremental_write_varint(&writer, program->fast_passes);
    incremental_write_varint(&writer, IR_ABI_CONVENTION_COUNT);
    for (u32 convention = 0; convention < IR_ABI_CONVENTION_COUNT; convention += 1)
    {
        incremental_write_u8(&writer, program->abi_contexts[convention].sysv_unnamed_bitfields_integer);
    }
    return incremental_writer_finish(&writer);
}

// ---------------------------------------------------------------------------
// Referenced ids. A generation stamp per program id marks membership for the
// function being recorded, so no per-function clear is paid. Collected ids
// are sorted and their ranks published through the parallel rank arrays.

BUSTER_GLOBAL_LOCAL void incremental_next_generation(IncrementalCodegenSession* session)
{
    session->generation += 1;
    if (!session->generation)
    {
        memset(session->symbol_stamps, 0, sizeof(u32) * (u64)session->symbol_capacity);
        memset(session->type_stamps, 0, sizeof(u32) * (u64)session->type_capacity);
        session->generation = 1;
    }
}

BUSTER_GLOBAL_LOCAL void incremental_push_symbol(IncrementalCodegenSession* session, u32* count, IrSymbolId symbol)
{
    if (symbol.value < session->symbol_capacity && session->symbol_stamps[symbol.value] != session->generation)
    {
        session->symbol_stamps[symbol.value] = session->generation;
        session->symbol_list[*count] = symbol.value;
        *count += 1;
    }
}

BUSTER_GLOBAL_LOCAL void incremental_push_type(IncrementalCodegenSession* session, u32* count, IrTypeId type)
{
    if (type.value < session->type_capacity && session->type_stamps[type.value] != session->generation)
    {
        session->type_stamps[type.value] = session->generation;
        session->type_list[*count] = type.value;
        *count += 1;
    }
}

BUSTER_GLOBAL_LOCAL u32 incremental_collect_symbols(IncrementalCodegenSession* session, IrFunction* function)
{
    u32 count = 0;
    incremental_push_symbol(session, &count, function->symbol);
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        incremental_push_symbol(session, &count, function->instructions[index].symbol);
    }
    IncrementalSortInput input = {.kind = INCREMENTAL_SORT_U32};
    incremental_sort_indices(&input, session->symbol_list, count);
    for (u32 rank = 0; rank < count; rank += 1)
    {
        session->symbol_ranks[session->symbol_list[rank]] = rank;
    }
    return count;
}

// The complete type closure: every type the function names, every type its
// referenced symbols name, and everything reachable from those through
// element, return, unqualified, parameter and field types. The collection
// list doubles as the worklist, so the walk is iterative and visits each type
// once.
BUSTER_GLOBAL_LOCAL u32 incremental_collect_types(IncrementalCodegenSession* session, IrProgram* program, IrFunction* function, u32 symbol_count)
{
    u32 count = 0;
    incremental_push_type(session, &count, function->canonical_type);
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        incremental_push_type(session, &count, function->instructions[index].canonical_type);
    }
    for (u32 index = 0; index < function->value_count; index += 1)
    {
        incremental_push_type(session, &count, function->values[index].canonical_type);
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        for (IrBlockParameter* parameter = function->blocks[block_index].first_parameter; parameter; parameter = parameter->next)
        {
            incremental_push_type(session, &count, parameter->canonical_type);
        }
    }
    if (function->published_cfg)
    {
        for (u32 index = 0; index < function->published_cfg->parameter_count; index += 1)
        {
            incremental_push_type(session, &count, function->published_cfg->parameters[index].canonical_type);
        }
    }
    for (u32 index = 0; index < function->debug_local_count; index += 1)
    {
        incremental_push_type(session, &count, function->debug_locals[index].type);
    }
    for (u32 index = 0; index < symbol_count; index += 1)
    {
        incremental_push_type(session, &count, program->symbols.symbols[session->symbol_list[index]].type);
    }
    for (u32 cursor = 0; cursor < count; cursor += 1)
    {
        IrType const* type = program->types.types + session->type_list[cursor];
        incremental_push_type(session, &count, type->element_type);
        incremental_push_type(session, &count, type->return_type);
        incremental_push_type(session, &count, type->unqualified_type);
        for (u32 index = 0; type->parameter_types && index < type->parameter_count; index += 1)
        {
            incremental_push_type(session, &count, type->parameter_types[index]);
        }
        for (u32 index = 0; type->fields && index < type->field_count; index += 1)
        {
            incremental_push_type(session, &count, type->fields[index].type);
        }
    }
    IncrementalSortInput input = {.kind = INCREMENTAL_SORT_U32};
    incremental_sort_indices(&input, session->type_list, count);
    for (u32 rank = 0; rank < count; rank += 1)
    {
        session->type_ranks[session->type_list[rank]] = rank;
    }
    return count;
}

// A reference outside the collected set would mean an id the record cannot
// relabel; the record fails instead of silently widening the equivalence.
BUSTER_GLOBAL_LOCAL void incremental_write_type(IncrementalCodegenSession* session, IncrementalWriter* writer, IrTypeId type)
{
    u32 value = IR_ID_UNDERLYING_INVALID;
    if (type.value != IR_ID_UNDERLYING_INVALID)
    {
        if (type.value < session->type_capacity && session->type_stamps[type.value] == session->generation)
        {
            value = session->type_ranks[type.value];
        }
        else
        {
            writer->failed = true;
        }
    }
    incremental_write_id(writer, value);
}

BUSTER_GLOBAL_LOCAL void incremental_write_symbol(IncrementalCodegenSession* session, IncrementalWriter* writer, IrSymbolId symbol)
{
    u32 value = IR_ID_UNDERLYING_INVALID;
    if (symbol.value != IR_ID_UNDERLYING_INVALID)
    {
        if (symbol.value < session->symbol_capacity && session->symbol_stamps[symbol.value] == session->generation)
        {
            value = session->symbol_ranks[symbol.value];
        }
        else
        {
            writer->failed = true;
        }
    }
    incremental_write_id(writer, value);
}

// A counted array of ids. The header is zero for a null array and the count
// plus one otherwise: consumers test the pointer, so null and empty differ.
BUSTER_GLOBAL_LOCAL void incremental_write_id_array(IncrementalWriter* writer, u32 const* values, u64 count)
{
    incremental_write_varint(writer, values ? count + 1 : 0);
    for (u64 index = 0; values && index < count; index += 1)
    {
        incremental_write_id(writer, values[index]);
    }
}

// Value ids referenced by consecutive rows are close to one another, so a
// stream of them is recorded as signed distances from the previous id in the
// same stream. Zero is the absent id; a present id is its zigzag distance plus
// one. Decoding needs only the stream order, so the encoding stays injective.
BUSTER_GLOBAL_LOCAL void incremental_write_delta_id(IncrementalWriter* writer, u32* cursor, u32 id)
{
    if (id == IR_ID_UNDERLYING_INVALID)
    {
        incremental_write_varint(writer, 0);
    }
    else
    {
        s64 delta = (s64)id - (s64)*cursor;
        incremental_write_varint(writer, (((u64)delta << 1) ^ (u64)(delta >> 63)) + 1);
        *cursor = id;
    }
}

BUSTER_GLOBAL_LOCAL void incremental_write_delta_array(IncrementalWriter* writer, u32* cursor, u32 const* values, u64 count)
{
    incremental_write_varint(writer, values ? count + 1 : 0);
    for (u64 index = 0; values && index < count; index += 1)
    {
        incremental_write_delta_id(writer, cursor, values[index]);
    }
}

// ---------------------------------------------------------------------------
// The canonical dependency record of one prepared function.

BUSTER_GLOBAL_LOCAL void incremental_record_body(IncrementalCodegenSession* session, IncrementalWriter* writer, IrFunction* function)
{
    incremental_write_type(session, writer, function->canonical_type);
    incremental_write_symbol(session, writer, function->symbol);
    incremental_write_varint(writer, (u64)function->state);
    incremental_write_id(writer, function->entry.value);
    incremental_write_varint(writer, function->block_count);
    incremental_write_varint(writer, function->instruction_count);
    incremental_write_varint(writer, function->value_count);
    incremental_write_varint(writer, function->local_count);
    incremental_write_varint(writer, function->debug_local_count);
    incremental_write_varint(writer, function->label_metadata_count);
    incremental_write_varint(writer, function->extra_count);
    incremental_write_varint(writer, function->opcode_summary);
    incremental_write_varint(writer, function->operand_total);
    incremental_write_varint(writer, function->operand_total_rows);
    u32 value_cursor = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction const* instruction = function->instructions + index;
        // The seven narrowed operation/order bytes hold their enum's COUNT
        // sentinel when the row is not of that kind; a mask names the ones
        // that do not, and only those bytes follow.
        u8 kinds[] = {instruction->conversion_operation, instruction->unary_operation, instruction->binary_operation, instruction->memory_order,
                      instruction->failure_memory_order, instruction->atomic_operation, instruction->simd_operation};
        u8 const sentinels[] = {IR_CONVERSION_COUNT, IR_UNARY_COUNT, IR_BINARY_COUNT, IR_MEMORY_ORDER_COUNT, IR_MEMORY_ORDER_COUNT,
                                IR_ATOMIC_OPERATION_COUNT, IR_SIMD_COUNT};
        u8 flags = (u8)((u8)instruction->immediate_is_negative | (u8)((u8)instruction->atomic_signal_fence << 1) |
                        (u8)((u8)instruction->volatile_access << 2));
        u8 mask = flags ? 0x80 : 0;
        for (u32 kind = 0; kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
        {
            mask |= (u8)(kinds[kind] != sentinels[kind] ? 1u << kind : 0u);
        }
        incremental_write_u8(writer, instruction->opcode);
        incremental_write_u8(writer, mask);
        for (u32 kind = 0; kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
        {
            if (mask & (1u << kind))
            {
                incremental_write_u8(writer, kinds[kind]);
            }
        }
        if (flags)
        {
            incremental_write_u8(writer, flags);
        }
        incremental_write_type(session, writer, instruction->canonical_type);
        incremental_write_symbol(session, writer, instruction->symbol);
        incremental_write_id(writer, instruction->canonical_local.value);
        // The successor link is almost always the next row: recorded as the
        // zigzag distance from it plus one, with zero for the absent link.
        u64 next = 0;
        if (instruction->next.value != IR_ID_UNDERLYING_INVALID)
        {
            s64 delta = (s64)instruction->next.value - (s64)index - 1;
            next = (((u64)delta << 1) ^ (u64)(delta >> 63)) + 1;
        }
        incremental_write_varint(writer, next);
        incremental_write_delta_id(writer, &value_cursor, instruction->result.value);
        incremental_write_delta_array(writer, &value_cursor, (u32 const*)instruction->operands, instruction->operand_count);
        incremental_write_id_array(writer, (u32 const*)instruction->targets, instruction->target_count);
        incremental_write_varint(writer, instruction->immediates ? (u64)instruction->immediate_count + 1 : 0);
        for (u32 immediate = 0; instruction->immediates && immediate < instruction->immediate_count; immediate += 1)
        {
            incremental_write_varint(writer, instruction->immediates[immediate]);
        }
    }
    u32 definition_cursor = 0;
    for (u32 index = 0; index < function->value_count; index += 1)
    {
        IrValue const* value = function->values + index;
        incremental_write_type(session, writer, value->canonical_type);
        incremental_write_delta_id(writer, &definition_cursor, value->definition.value);
        incremental_write_varint(writer, value->alignment);
        incremental_write_u8(writer, value->category);
        incremental_write_u8(writer, (u8)((u8)value->is_read_only | (u8)((u8)value->points_to_read_only << 1) | (u8)((u8)value->is_volatile << 2)));
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock const* block = function->blocks + block_index;
        incremental_write_id(writer, block->first_instruction.value);
        incremental_write_id(writer, block->last_instruction.value);
        incremental_write_id(writer, block->id.value);
        incremental_write_varint(writer, block->parameter_count);
        incremental_write_varint(writer, block->predecessor_count);
        incremental_write_u8(writer, (u8)((u8)block->terminated | (u8)((u8)block->sealed << 1)));
        for (IrBlockParameter const* parameter = block->first_parameter; parameter; parameter = parameter->next)
        {
            incremental_write_u8(writer, 1);
            incremental_write_type(session, writer, parameter->canonical_type);
            incremental_write_id(writer, parameter->canonical_local.value);
            incremental_write_id(writer, parameter->value.value);
            incremental_write_varint(writer, parameter->incoming_count);
            for (IrIncoming const* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
            {
                incremental_write_u8(writer, 1);
                incremental_write_id(writer, incoming->predecessor.value);
                incremental_write_id(writer, incoming->value.value);
            }
            incremental_write_u8(writer, 0);
        }
        incremental_write_u8(writer, 0);
        for (IrPredecessor const* predecessor = block->first_predecessor; predecessor; predecessor = predecessor->next)
        {
            incremental_write_u8(writer, 1);
            incremental_write_id(writer, predecessor->block.value);
        }
        incremental_write_u8(writer, 0);
        incremental_write_id_array(writer, (u32 const*)block->local_values, function->local_count);
    }
    IrPublishedCfg const* cfg = function->published_cfg;
    incremental_write_u8(writer, cfg != 0);
    if (cfg)
    {
        incremental_write_id_array(writer, (u32 const*)cfg->instruction_remap, function->instruction_count);
        u32 pool_cursor = 0;
        incremental_write_delta_array(writer, &pool_cursor, (u32 const*)cfg->operand_pool, cfg->operand_count);
        incremental_write_id_array(writer, (u32 const*)cfg->target_pool, cfg->target_count);
        incremental_write_u8(writer, cfg->immediate_pool != 0);
        incremental_write_varint(writer, cfg->immediate_pool ? cfg->immediate_count : 0);
        for (u64 index = 0; cfg->immediate_pool && index < cfg->immediate_count; index += 1)
        {
            incremental_write_varint(writer, cfg->immediate_pool[index]);
        }
        incremental_write_varint(writer, cfg->block_count);
        incremental_write_varint(writer, cfg->instruction_count);
        incremental_write_u8(writer, cfg->blocks != 0);
        for (u32 index = 0; cfg->blocks && index < cfg->block_count; index += 1)
        {
            IrCfgBlock const* block = cfg->blocks + index;
            incremental_write_varint(writer, block->first_instruction);
            incremental_write_varint(writer, block->instruction_count);
            incremental_write_varint(writer, block->successor_offset);
            incremental_write_varint(writer, block->successor_count);
            incremental_write_varint(writer, block->predecessor_offset);
            incremental_write_varint(writer, block->predecessor_count);
            incremental_write_varint(writer, block->parameter_offset);
            incremental_write_varint(writer, block->parameter_count);
        }
        incremental_write_varint(writer, cfg->edge_count);
        incremental_write_u8(writer, cfg->edges != 0);
        for (u32 index = 0; cfg->edges && index < cfg->edge_count; index += 1)
        {
            incremental_write_id(writer, cfg->edges[index].source.value);
            incremental_write_id(writer, cfg->edges[index].destination.value);
            incremental_write_varint(writer, cfg->edges[index].argument_offset);
        }
        incremental_write_id_array(writer, cfg->predecessors, cfg->edge_count);
        incremental_write_varint(writer, cfg->parameter_count);
        incremental_write_u8(writer, cfg->parameters != 0);
        for (u32 index = 0; cfg->parameters && index < cfg->parameter_count; index += 1)
        {
            incremental_write_type(session, writer, cfg->parameters[index].canonical_type);
            incremental_write_id(writer, cfg->parameters[index].canonical_local.value);
            incremental_write_id(writer, cfg->parameters[index].value.value);
        }
        incremental_write_id_array(writer, (u32 const*)cfg->arguments, cfg->argument_count);
    }
    incremental_write_id_array(writer, (u32 const*)function->local_places, function->local_count);
    incremental_write_u8(writer, function->local_uses_memory != 0);
    for (u32 index = 0; function->local_uses_memory && index < function->local_count; index += 1)
    {
        incremental_write_u8(writer, function->local_uses_memory[index]);
    }
    for (u32 index = 0; index < function->debug_local_count; index += 1)
    {
        IrDebugLocal const* local = function->debug_locals + index;
        incremental_write_id(writer, local->id.value);
        incremental_write_type(session, writer, local->type);
        incremental_write_varint(writer, local->scope_depth);
        incremental_write_u8(writer, local->is_parameter);
    }
    for (u32 index = 0; index < function->label_metadata_count; index += 1)
    {
        IrValueLabelMetadata const* metadata = function->label_metadata + index;
        incremental_write_id(writer, function->label_metadata_values[index].value);
        incremental_write_u8(writer, (u8)((u8)metadata->is_label_value | (u8)((u8)metadata->has_label_provenance << 1) |
                                          (u8)((u8)metadata->has_non_label_provenance << 2)));
        incremental_write_id_array(writer, (u32 const*)metadata->label_blocks, metadata->label_block_count);
        incremental_write_u8(writer, metadata->label_paths != 0);
        incremental_write_varint(writer, metadata->label_paths ? metadata->label_path_count : 0);
        for (u32 path_index = 0; metadata->label_paths && path_index < metadata->label_path_count; path_index += 1)
        {
            IrLabelProvenancePath const* path = metadata->label_paths + path_index;
            incremental_write_id_array(writer, (u32 const*)path->label_blocks, path->label_block_count);
            incremental_write_varint(writer, path->offset);
            incremental_write_varint(writer, path->size);
            incremental_write_u8(writer, path->is_non_label);
        }
    }
    for (u32 index = 0; index < function->extra_count; index += 1)
    {
        IrInstructionExtra const* extra = function->extras + index;
        incremental_write_id(writer, function->extra_instructions[index].value);
        incremental_write_string(writer, extra->literal);
        incremental_write_u8(writer, extra->label_names != 0);
        incremental_write_varint(writer, extra->label_names ? extra->label_name_count : 0);
        for (u32 name = 0; extra->label_names && name < extra->label_name_count; name += 1)
        {
            incremental_write_string(writer, extra->label_names[name]);
        }
        incremental_write_u8(writer, extra->operand_names != 0);
        incremental_write_varint(writer, extra->operand_names ? extra->operand_name_count : 0);
        for (u32 name = 0; extra->operand_names && name < extra->operand_name_count; name += 1)
        {
            incremental_write_string(writer, extra->operand_names[name]);
        }
        incremental_write_u8(writer, extra->clobbers != 0);
        incremental_write_varint(writer, extra->clobbers ? extra->clobber_count : 0);
        for (u32 name = 0; extra->clobbers && name < extra->clobber_count; name += 1)
        {
            incremental_write_string(writer, extra->clobbers[name]);
        }
    }
}

BUSTER_GLOBAL_LOCAL void incremental_record_types(IncrementalCodegenSession* session, IncrementalWriter* writer, IrProgram* program, u32 type_count)
{
    incremental_write_varint(writer, type_count);
    for (u32 rank = 0; rank < type_count; rank += 1)
    {
        IrType const* type = program->types.types + session->type_list[rank];
        u64 flags = (u64)type->is_signed | ((u64)type->is_variadic << 1) | ((u64)type->is_atomic << 2) | ((u64)type->is_nullptr << 3) |
                    ((u64)type->is_volatile << 4) | ((u64)type->is_transparent_union << 5) | ((u64)type->is_complex << 6) |
                    ((u64)type->is_noreturn << 7) | ((u64)type->is_unprototyped << 8) | ((u64)type->layout.resolved << 9) |
                    ((u64)(type->parameter_types != 0) << 10) | ((u64)(type->fields != 0) << 11) | ((u64)(type->enum_members != 0) << 12);
        incremental_write_varint(writer, (u64)type->kind);
        incremental_write_varint(writer, (u64)type->calling_convention);
        incremental_write_varint(writer, type->float_format);
        incremental_write_varint(writer, flags);
        incremental_write_varint(writer, type->layout.size);
        incremental_write_varint(writer, type->layout.alignment);
        incremental_write_varint(writer, (u64)type->layout.abi_class);
        incremental_write_varint(writer, type->layout.natural_alignment);
        incremental_write_varint(writer, type->element_count);
        incremental_write_varint(writer, type->bit_width);
        incremental_write_type(session, writer, type->element_type);
        incremental_write_type(session, writer, type->return_type);
        incremental_write_type(session, writer, type->unqualified_type);
        incremental_write_varint(writer, type->parameter_types ? type->parameter_count : 0);
        for (u32 index = 0; type->parameter_types && index < type->parameter_count; index += 1)
        {
            incremental_write_type(session, writer, type->parameter_types[index]);
        }
        incremental_write_varint(writer, type->fields ? type->field_count : 0);
        for (u32 index = 0; type->fields && index < type->field_count; index += 1)
        {
            IrField const* field = type->fields + index;
            incremental_write_type(session, writer, field->type);
            incremental_write_varint(writer, field->offset);
            incremental_write_varint(writer, field->bit_offset);
            incremental_write_varint(writer, field->bit_width);
            incremental_write_u8(writer, field->is_bit_field);
            incremental_write_u8(writer, field->access_size);
        }
        incremental_write_varint(writer, type->enum_members ? type->enum_member_count : 0);
        for (u32 index = 0; type->enum_members && index < type->enum_member_count; index += 1)
        {
            incremental_write_varint(writer, type->enum_members[index].value);
        }
    }
}

BUSTER_GLOBAL_LOCAL void incremental_record_symbols(IncrementalCodegenSession* session, IncrementalWriter* writer, IrProgram* program,
                                                    u32 symbol_count)
{
    incremental_write_varint(writer, symbol_count);
    for (u32 rank = 0; rank < symbol_count; rank += 1)
    {
        IrSymbol const* symbol = program->symbols.symbols + session->symbol_list[rank];
        incremental_write_varint(writer, (u64)symbol->kind);
        incremental_write_varint(writer, (u64)symbol->linkage);
        incremental_write_u8(writer, (u8)((u8)symbol->is_definition | (u8)((u8)symbol->is_thread_local << 1) | (u8)((u8)symbol->is_weak << 2) |
                                          (u8)((u8)symbol->is_hidden << 3)));
        incremental_write_string(writer, symbol->section_name);
        incremental_write_type(session, writer, symbol->type);
    }
}

BUSTER_GLOBAL_LOCAL bool incremental_record_function(IncrementalCodegenSession* session, IrProgram* program, IrFunction* function,
                                                     IncrementalFunctionState* state)
{
    incremental_next_generation(session);
    u32 symbol_count = incremental_collect_symbols(session, function);
    u32 type_count = incremental_collect_types(session, program, function, symbol_count);
    IncrementalWriter writer = incremental_writer_begin(session->arena);
    incremental_write_varint(&writer, INCREMENTAL_RECORD_MAGIC);
    incremental_write_varint(&writer, INCREMENTAL_KEY_SCHEMA);
    u64 section_start[INCREMENTAL_SECTION_COUNT + 1];
    section_start[INCREMENTAL_SECTION_BODY] = writer.count;
    incremental_record_body(session, &writer, function);
    section_start[INCREMENTAL_SECTION_TYPES] = writer.count;
    incremental_record_types(session, &writer, program, type_count);
    section_start[INCREMENTAL_SECTION_SYMBOLS] = writer.count;
    incremental_record_symbols(session, &writer, program, symbol_count);
    section_start[INCREMENTAL_SECTION_COUNT] = writer.count;
    ByteSlice record = incremental_writer_finish(&writer);
    bool result = record.pointer != 0;
    if (result)
    {
        state->record = record;
        state->fingerprint = incremental_fingerprint(record);
        for (u32 section = 0; section < INCREMENTAL_SECTION_COUNT; section += 1)
        {
            state->sections[section] = incremental_fingerprint((ByteSlice){.pointer = record.pointer + section_start[section],
                                                                           .length = section_start[section + 1] - section_start[section]});
        }
#if BUSTER_INCREMENTAL_AUDIT
        state->audit_type_count = type_count;
        state->audit_types = arena_allocate(session->arena, u32, type_count ? type_count : 1);
        memcpy(state->audit_types, session->type_list, sizeof(u32) * (u64)type_count);
#endif
        state->symbol_slot_count = symbol_count;
        state->symbol_slots = arena_allocate(session->arena, IrSymbolId, symbol_count ? symbol_count : 1);
        for (u32 index = 0; index < symbol_count; index += 1)
        {
            state->symbol_slots[index] = (IrSymbolId){.value = session->symbol_list[index]};
        }
        session->statistics.record_bytes += record.length;
        session->statistics.record_body_bytes += section_start[INCREMENTAL_SECTION_TYPES] - section_start[INCREMENTAL_SECTION_BODY];
        session->statistics.record_type_bytes += section_start[INCREMENTAL_SECTION_SYMBOLS] - section_start[INCREMENTAL_SECTION_TYPES];
        session->statistics.record_symbol_bytes += section_start[INCREMENTAL_SECTION_COUNT] - section_start[INCREMENTAL_SECTION_SYMBOLS];
        session->statistics.record_types += type_count;
        session->statistics.record_symbols += symbol_count;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Artifact: what one machine-emitted function contributed to the module, with
// every module-relative quantity made function-relative and every symbol made
// a slot. The encoding is a fixed field order; decoding validates each count
// against the remaining bytes and each index against the current function.

BUSTER_GLOBAL_LOCAL void incremental_write_debug_location(IncrementalWriter* writer, DebugLocation const* location)
{
    incremental_write_varint(writer, (u64)location->kind);
    incremental_write_varint(writer, (u64)location->reg);
    incremental_write_signed(writer, location->frame_offset);
    incremental_write_varint(writer, location->constant);
    incremental_write_varint(writer, location->pieces ? location->piece_count : 0);
    for (u32 index = 0; location->pieces && index < location->piece_count; index += 1)
    {
        DebugLocationPiece const* piece = location->pieces + index;
        incremental_write_varint(writer, (u64)piece->kind);
        incremental_write_varint(writer, (u64)piece->reg);
        incremental_write_signed(writer, piece->frame_offset);
        incremental_write_varint(writer, piece->constant);
        incremental_write_varint(writer, piece->value_offset);
        incremental_write_varint(writer, piece->size);
    }
}

BUSTER_GLOBAL_LOCAL void incremental_write_offsets(IncrementalWriter* writer, u32 const* values, u32 count)
{
    incremental_write_varint(writer, values ? count : 0);
    for (u32 index = 0; values && index < count; index += 1)
    {
        incremental_write_varint(writer, values[index]);
    }
}

ByteSlice incremental_artifact_encode(Arena* arena, IncrementalFunctionArtifact const* artifact)
{
    IncrementalWriter writer = incremental_writer_begin(arena);
    incremental_write_varint(&writer, INCREMENTAL_ARTIFACT_MAGIC);
    writer.failed = writer.failed || artifact->code.length > INCREMENTAL_MAX_CODE_BYTES;
    incremental_write_varint(&writer, artifact->code.length);
    incremental_write_bytes(&writer, artifact->code.pointer, artifact->code.length);
    incremental_write_varint(&writer, artifact->prolog_size);
    incremental_write_varint(&writer, artifact->symbol_slot_count);
    incremental_write_varint(&writer, artifact->unwind_action_count);
    for (u32 index = 0; index < artifact->unwind_action_count; index += 1)
    {
        CodegenUnwindAction const* action = artifact->unwind_actions + index;
        incremental_write_varint(&writer, action->code_offset);
        incremental_write_varint(&writer, action->value);
        incremental_write_u8(&writer, (u8)action->kind);
        incremental_write_u8(&writer, action->register_index);
    }
    incremental_write_offsets(&writer, artifact->epilog_offsets, artifact->epilog_count);
    incremental_write_offsets(&writer, artifact->block_offsets, artifact->block_count);
    incremental_write_varint(&writer, artifact->relocation_count);
    for (u32 index = 0; index < artifact->relocation_count; index += 1)
    {
        IncrementalRelocation const* relocation = artifact->relocations + index;
        incremental_write_varint(&writer, relocation->offset);
        incremental_write_varint(&writer, relocation->symbol_slot);
        incremental_write_signed(&writer, relocation->addend);
        incremental_write_u8(&writer, relocation->kind);
    }
    // Marks advance through the code and the rows in step, so each is kept as
    // its distance from the one before.
    incremental_write_varint(&writer, artifact->line_mark_count);
    s64 previous_offset = 0;
    s64 previous_instruction = 0;
    for (u32 index = 0; index < artifact->line_mark_count; index += 1)
    {
        IncrementalLineMark mark = artifact->line_marks[index];
        incremental_write_signed(&writer, (s64)mark.code_offset - previous_offset);
        incremental_write_signed(&writer, (s64)mark.instruction - previous_instruction);
        previous_offset = mark.code_offset;
        previous_instruction = mark.instruction;
    }
    incremental_write_varint(&writer, artifact->debug_location_count);
    for (u32 index = 0; index < artifact->debug_location_count; index += 1)
    {
        IncrementalDebugLocation const* location = artifact->debug_locations + index;
        incremental_write_id(&writer, location->local.value);
        incremental_write_varint(&writer, location->start);
        incremental_write_varint(&writer, (u64)location->end - location->start);
        incremental_write_debug_location(&writer, &location->location);
    }
    incremental_write_varint(&writer, INCREMENTAL_STATISTIC_COUNT);
    for (u32 index = 0; index < INCREMENTAL_STATISTIC_COUNT; index += 1)
    {
        incremental_write_varint(&writer, artifact->statistics[index]);
    }
    return incremental_writer_finish(&writer);
}

BUSTER_GLOBAL_LOCAL bool incremental_read_debug_location(Arena* arena, IncrementalReader* reader, DebugLocation* location)
{
    *location = (DebugLocation){0};
    location->kind = (DebugLocationKind)incremental_read_u32_varint(reader);
    location->reg = (DebugRegister)incremental_read_u32_varint(reader);
    s64 frame_offset = incremental_read_signed(reader);
    location->constant = incremental_read_varint(reader);
    location->piece_count = incremental_read_u32_varint(reader);
    bool result = !reader->failed && (u32)location->kind < DEBUG_LOCATION_COUNT && frame_offset >= INT32_MIN && frame_offset <= INT32_MAX &&
                  location->piece_count <= INCREMENTAL_MAX_DEBUG_PIECES;
    location->frame_offset = result ? (s32)frame_offset : 0;
    if (result && location->piece_count)
    {
        location->pieces = arena_allocate(arena, DebugLocationPiece, location->piece_count);
        for (u32 index = 0; index < location->piece_count; index += 1)
        {
            DebugLocationPiece* piece = location->pieces + index;
            *piece = (DebugLocationPiece){0};
            piece->kind = (DebugLocationKind)incremental_read_u32_varint(reader);
            piece->reg = (DebugRegister)incremental_read_u32_varint(reader);
            s64 piece_offset = incremental_read_signed(reader);
            piece->constant = incremental_read_varint(reader);
            piece->value_offset = incremental_read_u32_varint(reader);
            piece->size = incremental_read_u32_varint(reader);
            result = result && (u32)piece->kind < DEBUG_LOCATION_COUNT && piece_offset >= INT32_MIN && piece_offset <= INT32_MAX;
            piece->frame_offset = result ? (s32)piece_offset : 0;
        }
    }
    return result && !reader->failed;
}

// The highest local id a debug seed of this function may name: seeds come
// from debug locals and canonical local slots, both below this bound.
BUSTER_GLOBAL_LOCAL u32 incremental_local_bound(IrFunction const* function)
{
    u32 result = function->local_count;
    for (u32 index = 0; index < function->debug_local_count; index += 1)
    {
        u32 id = function->debug_locals[index].id.value;
        if (id != IR_ID_UNDERLYING_INVALID && id >= result)
        {
            result = id + 1;
        }
    }
    return result;
}

// Reads a count and refuses one the remaining bytes cannot back: every
// element costs at least one encoded byte.
BUSTER_GLOBAL_LOCAL u32 incremental_read_count(IncrementalReader* reader)
{
    u32 result = incremental_read_u32_varint(reader);
    if (result > reader->length - reader->cursor)
    {
        reader->failed = true;
        result = 0;
    }
    return result;
}

bool incremental_artifact_decode(Arena* arena, ByteSlice bytes, IrFunction const* function, u32 symbol_slot_count,
                                 IncrementalFunctionArtifact* artifact)
{
    IncrementalReader reader = {.bytes = bytes.pointer, .length = bytes.length};
    *artifact = (IncrementalFunctionArtifact){0};
    bool result = incremental_read_varint(&reader) == INCREMENTAL_ARTIFACT_MAGIC;
    u32 code_size = incremental_read_u32_varint(&reader);
    result = result && code_size <= INCREMENTAL_MAX_CODE_BYTES;
    u8 const* code = result ? incremental_reader_take(&reader, code_size) : 0;
    artifact->code = (ByteSlice){.pointer = (u8*)code, .length = code ? code_size : 0};
    artifact->prolog_size = incremental_read_u32_varint(&reader);
    artifact->symbol_slot_count = incremental_read_u32_varint(&reader);
    result = result && !reader.failed && artifact->prolog_size <= code_size && artifact->symbol_slot_count == symbol_slot_count;
    u32 unwind_count = result ? incremental_read_count(&reader) : 0;
    if (result && unwind_count)
    {
        artifact->unwind_actions = arena_allocate(arena, CodegenUnwindAction, unwind_count);
        for (u32 index = 0; index < unwind_count; index += 1)
        {
            CodegenUnwindAction* action = artifact->unwind_actions + index;
            *action = (CodegenUnwindAction){0};
            action->code_offset = incremental_read_u32_varint(&reader);
            action->value = incremental_read_u32_varint(&reader);
            action->kind = (CodegenUnwindActionKind)incremental_read_u8(&reader);
            action->register_index = incremental_read_u8(&reader);
            result = result && action->code_offset <= code_size && (u32)action->kind < CODEGEN_UNWIND_ACTION_COUNT;
        }
    }
    artifact->unwind_action_count = unwind_count;
    u32 epilog_count = result ? incremental_read_count(&reader) : 0;
    if (result && epilog_count)
    {
        artifact->epilog_offsets = arena_allocate(arena, u32, epilog_count);
        for (u32 index = 0; index < epilog_count; index += 1)
        {
            artifact->epilog_offsets[index] = incremental_read_u32_varint(&reader);
            result = result && artifact->epilog_offsets[index] <= code_size;
        }
    }
    artifact->epilog_count = epilog_count;
    u32 block_count = result ? incremental_read_count(&reader) : 0;
    result = result && block_count == function->block_count;
    if (result && block_count)
    {
        artifact->block_offsets = arena_allocate(arena, u32, block_count);
        for (u32 index = 0; index < block_count; index += 1)
        {
            artifact->block_offsets[index] = incremental_read_u32_varint(&reader);
            result = result && artifact->block_offsets[index] <= code_size;
        }
    }
    artifact->block_count = block_count;
    u32 relocation_count = result ? incremental_read_count(&reader) : 0;
    if (result && relocation_count)
    {
        artifact->relocations = arena_allocate(arena, IncrementalRelocation, relocation_count);
        for (u32 index = 0; index < relocation_count; index += 1)
        {
            IncrementalRelocation* relocation = artifact->relocations + index;
            *relocation = (IncrementalRelocation){0};
            relocation->offset = incremental_read_u32_varint(&reader);
            relocation->symbol_slot = incremental_read_u32_varint(&reader);
            relocation->addend = incremental_read_signed(&reader);
            relocation->kind = incremental_read_u8(&reader);
            u32 width = relocation->kind == CODEGEN_MODULE_RELOCATION_ABSOLUTE64 ? 8u : 4u;
            result = result && codegen_module_relocation_kind_valid(relocation->kind) && relocation->symbol_slot < symbol_slot_count &&
                     relocation->offset <= code_size && width <= code_size - relocation->offset;
        }
    }
    artifact->relocation_count = relocation_count;
    u32 mark_count = result ? incremental_read_count(&reader) : 0;
    if (result && mark_count)
    {
        artifact->line_marks = arena_allocate(arena, IncrementalLineMark, mark_count);
        s64 offset = 0;
        s64 instruction = 0;
        for (u32 index = 0; index < mark_count && result; index += 1)
        {
            offset += incremental_read_signed(&reader);
            instruction += incremental_read_signed(&reader);
            result = offset >= 0 && offset <= code_size && instruction >= 0 && instruction < function->instruction_count;
            artifact->line_marks[index] = (IncrementalLineMark){.code_offset = result ? (u32)offset : 0, .instruction = result ? (u32)instruction : 0};
        }
    }
    artifact->line_mark_count = mark_count;
    u32 location_count = result ? incremental_read_count(&reader) : 0;
    u32 local_bound = incremental_local_bound(function);
    if (result && location_count)
    {
        artifact->debug_locations = arena_allocate(arena, IncrementalDebugLocation, location_count);
        for (u32 index = 0; index < location_count && result; index += 1)
        {
            IncrementalDebugLocation* location = artifact->debug_locations + index;
            *location = (IncrementalDebugLocation){0};
            location->local = (IrLocalId){.value = incremental_read_u32_varint(&reader) - 1u};
            location->start = incremental_read_u32_varint(&reader);
            u32 length = incremental_read_u32_varint(&reader);
            result = incremental_read_debug_location(arena, &reader, &location->location) && location->local.value < local_bound &&
                     location->start <= code_size && length <= code_size - location->start;
            location->end = location->start + length;
        }
    }
    artifact->debug_location_count = location_count;
    u32 statistic_count = result ? incremental_read_u32_varint(&reader) : 0;
    result = result && statistic_count == INCREMENTAL_STATISTIC_COUNT;
    for (u32 index = 0; result && index < INCREMENTAL_STATISTIC_COUNT; index += 1)
    {
        artifact->statistics[index] = incremental_read_varint(&reader);
    }
    result = result && !reader.failed && reader.cursor == reader.length;
    return result;
}

// ---------------------------------------------------------------------------
// Pack container. Header (INCREMENTAL_PACK_HEADER_SIZE bytes):
//   [0,8) magic   [8,12) format version   [12,16) key schema
//   [16,24) file size   [24,32) body fingerprint over [64, file size)
//   [32,36) context length   [36,40) entry count   [40,44) manifest count
//   [44,48) zero   [48,56) header fingerprint over [0,48)   [56,64) zero
// Body: context bytes, the entry table (fingerprint u64, record offset u64,
// record length u32, artifact length u32, artifact offset u64) sorted by
// (fingerprint, record bytes), the manifest table (name offset u64, name
// length u32, entry index u32, record fingerprint u64, three section
// fingerprints u64) sorted by name, then the name, record and artifact bytes.

BUSTER_GLOBAL_LOCAL bool incremental_range_valid(u64 file_size, u64 offset, u64 length)
{
    return offset >= INCREMENTAL_PACK_HEADER_SIZE && offset <= file_size && length <= file_size - offset;
}

IncrementalPackStatus incremental_pack_decode(Arena* arena, ByteSlice file, IncrementalPack* pack)
{
    *pack = (IncrementalPack){0};
    IncrementalPackStatus result = INCREMENTAL_PACK_CORRUPT;
    bool valid = file.length >= INCREMENTAL_PACK_HEADER_SIZE && memory_compare(file.pointer, incremental_pack_magic, sizeof(incremental_pack_magic));
    if (valid)
    {
        u8 const* header = file.pointer;
        u32 version = incremental_load_u32(header + 8);
        u32 schema = incremental_load_u32(header + 12);
        u64 size = incremental_load_u64(header + 16);
        u64 body_fingerprint = incremental_load_u64(header + 24);
        u32 context_length = incremental_load_u32(header + 32);
        u32 entry_count = incremental_load_u32(header + 36);
        u32 manifest_count = incremental_load_u32(header + 40);
        u64 header_fingerprint = incremental_load_u64(header + 48);
        // The reserved header words are outside both fingerprints, so they
        // must be zero rather than ignored.
        valid = header_fingerprint == buster_hash_64((u8*)header, 48) && size == file.length && !incremental_load_u32(header + 44) &&
                !incremental_load_u64(header + 56);
        if (valid && (version != INCREMENTAL_FORMAT_VERSION || schema != INCREMENTAL_KEY_SCHEMA))
        {
            // A well-formed pack from another format is not corrupt; it is a
            // different context and is replaced on the next publication.
            result = INCREMENTAL_PACK_CONTEXT_CHANGED;
            valid = false;
        }
        valid = valid && body_fingerprint == buster_hash_64(file.pointer + INCREMENTAL_PACK_HEADER_SIZE, file.length - INCREMENTAL_PACK_HEADER_SIZE);
        u64 entry_table = (u64)INCREMENTAL_PACK_HEADER_SIZE + context_length;
        u64 manifest_table = entry_table + (u64)entry_count * INCREMENTAL_PACK_ENTRY_SIZE;
        u64 tables_end = manifest_table + (u64)manifest_count * INCREMENTAL_PACK_MANIFEST_SIZE;
        valid = valid && tables_end <= file.length;
        if (valid)
        {
            pack->context = (ByteSlice){.pointer = file.pointer + INCREMENTAL_PACK_HEADER_SIZE, .length = context_length};
            pack->entries = arena_allocate(arena, IncrementalPackEntry, entry_count ? entry_count : 1);
            pack->manifest = arena_allocate(arena, IncrementalManifestEntry, manifest_count ? manifest_count : 1);
        }
        for (u32 index = 0; valid && index < entry_count; index += 1)
        {
            u8 const* row = file.pointer + entry_table + (u64)index * INCREMENTAL_PACK_ENTRY_SIZE;
            u64 record_offset = incremental_load_u64(row + 8);
            u32 record_length = incremental_load_u32(row + 16);
            u32 artifact_length = incremental_load_u32(row + 20);
            u64 artifact_offset = incremental_load_u64(row + 24);
            valid = incremental_range_valid(file.length, record_offset, record_length) &&
                    incremental_range_valid(file.length, artifact_offset, artifact_length) && record_offset >= tables_end &&
                    artifact_offset >= tables_end;
            if (valid)
            {
                pack->entries[index] = (IncrementalPackEntry){
                    .fingerprint = incremental_load_u64(row),
                    .record = {.pointer = file.pointer + record_offset, .length = record_length},
                    .artifact = {.pointer = file.pointer + artifact_offset, .length = artifact_length},
                };
                // The body fingerprint above already covers these bytes; order
                // is what the binary search needs and is checked here.
                valid = !index || incremental_entry_compare(pack->entries + index - 1, pack->entries + index) < 0;
            }
        }
        for (u32 index = 0; valid && index < manifest_count; index += 1)
        {
            u8 const* row = file.pointer + manifest_table + (u64)index * INCREMENTAL_PACK_MANIFEST_SIZE;
            u64 name_offset = incremental_load_u64(row);
            u32 name_length = incremental_load_u32(row + 8);
            u32 entry_index = incremental_load_u32(row + 12);
            valid = incremental_range_valid(file.length, name_offset, name_length) && name_offset >= tables_end &&
                    (entry_index == UINT32_MAX || entry_index < entry_count);
            if (valid)
            {
                IncrementalManifestEntry* entry = pack->manifest + index;
                *entry = (IncrementalManifestEntry){
                    .name = {.pointer = (char8*)(file.pointer + name_offset), .length = name_length},
                    .fingerprint = incremental_load_u64(row + 16),
                    .entry_index = entry_index,
                };
                for (u32 section = 0; section < INCREMENTAL_SECTION_COUNT; section += 1)
                {
                    entry->sections[section] = incremental_load_u64(row + 24 + 8 * section);
                }
                valid = !index || incremental_compare_bytes(pack->manifest[index - 1].name.pointer, pack->manifest[index - 1].name.length,
                                                            entry->name.pointer, entry->name.length) < 0;
            }
        }
        if (valid)
        {
            pack->entry_count = entry_count;
            pack->manifest_count = manifest_count;
            result = INCREMENTAL_PACK_LOADED;
        }
    }
    if (result != INCREMENTAL_PACK_LOADED)
    {
        *pack = (IncrementalPack){0};
    }
    return result;
}

u32 incremental_pack_find_entry(IncrementalPack const* pack, u64 fingerprint, ByteSlice record)
{
    u32 low = 0;
    u32 high = pack->entry_count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        if (pack->entries[middle].fingerprint < fingerprint)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    u32 result = UINT32_MAX;
    for (u32 index = low; index < pack->entry_count && pack->entries[index].fingerprint == fingerprint && result == UINT32_MAX; index += 1)
    {
        ByteSlice candidate = pack->entries[index].record;
        if (candidate.length == record.length && memory_compare(candidate.pointer, record.pointer, record.length))
        {
            result = index;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL IncrementalManifestEntry const* incremental_pack_find_name(IncrementalPack const* pack, String8 name)
{
    u32 low = 0;
    u32 high = pack->manifest_count;
    IncrementalManifestEntry const* result = 0;
    while (low < high && !result)
    {
        u32 middle = low + (high - low) / 2;
        String8 candidate = pack->manifest[middle].name;
        s32 order = incremental_compare_bytes(candidate.pointer, candidate.length, name.pointer, name.length);
        if (order < 0)
        {
            low = middle + 1;
        }
        else if (order > 0)
        {
            high = middle;
        }
        else
        {
            result = pack->manifest + middle;
        }
    }
    return result;
}

ByteSlice incremental_pack_encode(Arena* arena, ByteSlice context, IncrementalPackEntry* entries, u32 entry_count,
                                  IncrementalManifestEntry* manifest, u32 manifest_count)
{
    // Order and deduplicate: equal records name equal work, and one artifact
    // serves every function with that record.
    u32* entry_order = arena_allocate(arena, u32, entry_count ? entry_count : 1);
    for (u32 index = 0; index < entry_count; index += 1)
    {
        entry_order[index] = index;
    }
    IncrementalSortInput entry_input = {.entries = entries, .kind = INCREMENTAL_SORT_ENTRIES};
    incremental_sort_indices(&entry_input, entry_order, entry_count);
    u32* entry_rank = arena_allocate(arena, u32, entry_count ? entry_count : 1);
    u32 unique_count = 0;
    for (u32 position = 0; position < entry_count; position += 1)
    {
        u32 index = entry_order[position];
        if (unique_count && !incremental_entry_compare(entries + entry_order[unique_count - 1], entries + index))
        {
            entry_rank[index] = unique_count - 1;
        }
        else
        {
            entry_order[unique_count] = index;
            entry_rank[index] = unique_count;
            unique_count += 1;
        }
    }
    u32* manifest_order = arena_allocate(arena, u32, manifest_count ? manifest_count : 1);
    for (u32 index = 0; index < manifest_count; index += 1)
    {
        manifest_order[index] = index;
    }
    IncrementalSortInput manifest_input = {.manifest = manifest, .kind = INCREMENTAL_SORT_MANIFEST};
    incremental_sort_indices(&manifest_input, manifest_order, manifest_count);
    u32 unique_manifest = 0;
    for (u32 position = 0; position < manifest_count; position += 1)
    {
        u32 index = manifest_order[position];
        String8 name = manifest[index].name;
        String8 previous = unique_manifest ? manifest[manifest_order[unique_manifest - 1]].name : (String8){0};
        if (!unique_manifest || incremental_compare_bytes(previous.pointer, previous.length, name.pointer, name.length) != 0)
        {
            manifest_order[unique_manifest] = index;
            unique_manifest += 1;
        }
    }
    u64 tables_end = (u64)INCREMENTAL_PACK_HEADER_SIZE + context.length + (u64)unique_count * INCREMENTAL_PACK_ENTRY_SIZE +
                     (u64)unique_manifest * INCREMENTAL_PACK_MANIFEST_SIZE;
    u64 total = tables_end;
    for (u32 position = 0; position < unique_count; position += 1)
    {
        total += entries[entry_order[position]].record.length + entries[entry_order[position]].artifact.length;
    }
    for (u32 position = 0; position < unique_manifest; position += 1)
    {
        total += manifest[manifest_order[position]].name.length;
    }
    ByteSlice result = {0};
    bool valid = context.length <= UINT32_MAX;
    for (u32 position = 0; valid && position < unique_count; position += 1)
    {
        valid = entries[entry_order[position]].record.length <= UINT32_MAX && entries[entry_order[position]].artifact.length <= UINT32_MAX;
    }
    for (u32 position = 0; valid && position < unique_manifest; position += 1)
    {
        valid = manifest[manifest_order[position]].name.length <= UINT32_MAX;
    }
    if (valid)
    {
        u8* bytes = arena_allocate_zeroed(arena, u8, total);
        memcpy(bytes, incremental_pack_magic, sizeof(incremental_pack_magic));
        incremental_store_u32(bytes + 8, INCREMENTAL_FORMAT_VERSION);
        incremental_store_u32(bytes + 12, INCREMENTAL_KEY_SCHEMA);
        incremental_store_u64(bytes + 16, total);
        incremental_store_u32(bytes + 32, (u32)context.length);
        incremental_store_u32(bytes + 36, unique_count);
        incremental_store_u32(bytes + 40, unique_manifest);
        if (context.length)
        {
            memcpy(bytes + INCREMENTAL_PACK_HEADER_SIZE, context.pointer, context.length);
        }
        u64 entry_table = (u64)INCREMENTAL_PACK_HEADER_SIZE + context.length;
        u64 manifest_table = entry_table + (u64)unique_count * INCREMENTAL_PACK_ENTRY_SIZE;
        u64 cursor = tables_end;
        for (u32 position = 0; position < unique_count; position += 1)
        {
            IncrementalPackEntry const* entry = entries + entry_order[position];
            u8* row = bytes + entry_table + (u64)position * INCREMENTAL_PACK_ENTRY_SIZE;
            incremental_store_u64(row, entry->fingerprint);
            incremental_store_u64(row + 8, cursor);
            incremental_store_u32(row + 16, (u32)entry->record.length);
            incremental_store_u32(row + 20, (u32)entry->artifact.length);
            memcpy(bytes + cursor, entry->record.pointer, entry->record.length);
            cursor += entry->record.length;
            incremental_store_u64(row + 24, cursor);
            if (entry->artifact.length)
            {
                memcpy(bytes + cursor, entry->artifact.pointer, entry->artifact.length);
            }
            cursor += entry->artifact.length;
        }
        for (u32 position = 0; position < unique_manifest; position += 1)
        {
            IncrementalManifestEntry const* entry = manifest + manifest_order[position];
            u8* row = bytes + manifest_table + (u64)position * INCREMENTAL_PACK_MANIFEST_SIZE;
            incremental_store_u64(row, cursor);
            incremental_store_u32(row + 8, (u32)entry->name.length);
            incremental_store_u32(row + 12, entry->entry_index == UINT32_MAX ? UINT32_MAX : entry_rank[entry->entry_index]);
            incremental_store_u64(row + 16, entry->fingerprint);
            for (u32 section = 0; section < INCREMENTAL_SECTION_COUNT; section += 1)
            {
                incremental_store_u64(row + 24 + 8 * section, entry->sections[section]);
            }
            if (entry->name.length)
            {
                memcpy(bytes + cursor, entry->name.pointer, entry->name.length);
            }
            cursor += entry->name.length;
        }
        incremental_store_u64(bytes + 24, buster_hash_64(bytes + INCREMENTAL_PACK_HEADER_SIZE, total - INCREMENTAL_PACK_HEADER_SIZE));
        incremental_store_u64(bytes + 48, buster_hash_64(bytes, 48));
        result = (ByteSlice){.pointer = bytes, .length = total};
    }
    return result;
}

// ---------------------------------------------------------------------------
// Session lifecycle.

String8 incremental_pack_path(Arena* arena, String8 directory, String8 input_path)
{
    u64 name = buster_hash_64((u8*)input_path.pointer, input_path.length);
    bool separator = directory.length && (directory.pointer[directory.length - 1] == '/' || directory.pointer[directory.length - 1] == '\\');
    return string_format_z(arena, separator ? S8("{S8}{u64:x,no_prefix,width=[0,16]}.bpk") : S8("{S8}/{u64:x,no_prefix,width=[0,16]}.bpk"), directory, name);
}

// Atomic replacement without a flush: a concurrent reader sees the old pack or
// the new one, never a mixture, and a pack torn by a crash fails its size or
// fingerprint check and is treated as corrupt. Durability buys nothing here.
BUSTER_GLOBAL_LOCAL bool incremental_publish_file(Arena* arena, String8 directory, String8 path, ByteSlice bytes)
{
    bool result = os_make_directory_attempt(directory);
    OsFileStagingResult staging = result ? os_file_staging_create(arena, path, (OpenPermissions){.read = 1, .write = 1}) : (OsFileStagingResult){0};
    result = result && staging.file && !staging.error.v;
    if (result)
    {
        OsFileTransferResult written = os_file_write_checked(staging.file, bytes);
        OsError closed = os_file_close_checked(staging.file);
        result = !written.error.v && written.transferred == bytes.length && !closed.v && !os_file_replace(staging.path, path).v;
        if (!result)
        {
            os_file_delete_checked(staging.path);
        }
    }
    return result;
}

IncrementalCodegenSession* incremental_session_open(Arena* arena, IncrementalSessionOptions options)
{
    IncrementalCodegenSession* result = 0;
    Arena* session_arena = arena_create((ArenaCreation){.reserved_size = INCREMENTAL_SESSION_RESERVATION});
    if (session_arena)
    {
        result = arena_allocate(session_arena, IncrementalCodegenSession, 1);
        *result = (IncrementalCodegenSession){.arena = session_arena, .options = options};
        result->options.cache_directory = string_duplicate_arena(session_arena, options.cache_directory, false);
        result->options.input_path = string_duplicate_arena(session_arena, options.input_path, false);
        result->statistics.enabled = true;
        result->statistics.identity_bytes = options.compiler.image_size;
        result->statistics.identity_nanoseconds = options.compiler.nanoseconds;
        result->statistics.pack_status = INCREMENTAL_PACK_UNAVAILABLE;
        if (options.compiler.identified && options.cache_directory.length && options.input_path.length)
        {
            TimeDataType start = timestamp_take();
            result->pack_path = incremental_pack_path(session_arena, result->options.cache_directory, result->options.input_path);
            result->previous_file = file_map_read(session_arena, result->pack_path, (FileReadOptions){0});
            ByteSlice bytes = result->previous_file.bytes;
            if (bytes.pointer)
            {
                result->statistics.pack_bytes_read = bytes.length;
                result->statistics.pack_status = incremental_pack_decode(session_arena, bytes, &result->previous);
                result->previous_loaded = result->statistics.pack_status == INCREMENTAL_PACK_LOADED;
                result->statistics.pack_entries_read = result->previous.entry_count;
            }
            else
            {
                result->statistics.pack_status = INCREMENTAL_PACK_ABSENT;
            }
            result->statistics.pack_read_nanoseconds = timestamp_ns_between(start, timestamp_take());
            result->active = true;
        }
    }
    BUSTER_UNUSED(arena);
    return result;
}

BUSTER_GLOBAL_LOCAL IncrementalLookup incremental_classify_miss(IncrementalCodegenSession* session, IrFunction const* function,
                                                                IncrementalFunctionState const* state)
{
    IncrementalLookup result = INCREMENTAL_LOOKUP_MISS_NEW;
    IncrementalManifestEntry const* previous = incremental_pack_find_name(&session->previous, function->name);
    if (previous)
    {
        result = previous->sections[INCREMENTAL_SECTION_BODY] != state->sections[INCREMENTAL_SECTION_BODY]   ? INCREMENTAL_LOOKUP_MISS_BODY
                 : previous->sections[INCREMENTAL_SECTION_TYPES] != state->sections[INCREMENTAL_SECTION_TYPES] ? INCREMENTAL_LOOKUP_MISS_TYPES
                 : previous->sections[INCREMENTAL_SECTION_SYMBOLS] != state->sections[INCREMENTAL_SECTION_SYMBOLS]
                     ? INCREMENTAL_LOOKUP_MISS_SYMBOLS
                     : INCREMENTAL_LOOKUP_MISS_UNCAPTURED;
    }
    return result;
}

bool incremental_session_bind_module(IncrementalCodegenSession* session, IrProgram* program, IrModule* module, Target target,
                                     CodegenModuleOptions options, CodegenAbi abi, bool position_independent)
{
    bool result = session && session->active && !session->bound && program && module &&
                  options.register_allocator != CODEGEN_REGISTER_ALLOCATOR_NONE && !options.verify_invariants && !options.record_fallbacks &&
                  (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64);
    if (session && !result)
    {
        session->active = false;
    }
    if (result)
    {
        TimeDataType start = timestamp_take();
        Arena* arena = session->arena;
        session->bound = true;
        session->program = program;
        session->module = module;
        session->context = incremental_context_record(arena, session->options.compiler, program, target, options, abi, position_independent);
        result = session->context.pointer != 0;
        bool previous_usable = result && session->previous_loaded;
        if (previous_usable && !(session->previous.context.length == session->context.length &&
                                 memory_compare(session->previous.context.pointer, session->context.pointer, session->context.length)))
        {
            previous_usable = false;
            session->statistics.pack_status = INCREMENTAL_PACK_CONTEXT_CHANGED;
        }
        IncrementalLookup no_pack = session->statistics.pack_status == INCREMENTAL_PACK_CONTEXT_CHANGED ? INCREMENTAL_LOOKUP_MISS_CONTEXT
                                                                                                         : INCREMENTAL_LOOKUP_MISS_NO_PACK;
        session->function_count = module->function_count;
        session->functions = arena_allocate(arena, IncrementalFunctionState, module->function_count ? module->function_count : 1);
        session->symbol_capacity = program->symbols.count;
        session->type_capacity = program->types.count;
        session->symbol_stamps = arena_allocate_zeroed(arena, u32, session->symbol_capacity ? session->symbol_capacity : 1);
        session->symbol_ranks = arena_allocate(arena, u32, session->symbol_capacity ? session->symbol_capacity : 1);
        session->symbol_list = arena_allocate(arena, u32, session->symbol_capacity ? session->symbol_capacity : 1);
        session->type_stamps = arena_allocate_zeroed(arena, u32, session->type_capacity ? session->type_capacity : 1);
        session->type_ranks = arena_allocate(arena, u32, session->type_capacity ? session->type_capacity : 1);
        session->type_list = arena_allocate(arena, u32, session->type_capacity ? session->type_capacity : 1);
        for (u32 index = 0; result && index < module->function_count; index += 1)
        {
            IrFunction* function = module->functions + index;
            IncrementalFunctionState* state = session->functions + index;
            *state = (IncrementalFunctionState){.lookup = INCREMENTAL_LOOKUP_NOT_LOWERED};
            if (function->state == IR_FUNCTION_LOWERED)
            {
                session->statistics.lowered_ir_instructions += function->instruction_count;
                if (ir_function_may_contain_opcodes(function, IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)))
                {
                    state->lookup = INCREMENTAL_LOOKUP_INELIGIBLE_INLINE_ASSEMBLY;
                }
                else if (!incremental_record_function(session, program, function, state))
                {
                    state->lookup = INCREMENTAL_LOOKUP_INELIGIBLE_RECORD;
                }
                else if (!previous_usable)
                {
                    state->lookup = no_pack;
                }
                else
                {
                    u32 entry = incremental_pack_find_entry(&session->previous, state->fingerprint, state->record);
                    if (entry == UINT32_MAX)
                    {
                        state->lookup = incremental_classify_miss(session, function, state);
                    }
                    else if (incremental_artifact_decode(arena, session->previous.entries[entry].artifact, function, state->symbol_slot_count,
                                                         &state->reused))
                    {
                        state->lookup = INCREMENTAL_LOOKUP_REUSED;
                        state->stored_artifact = session->previous.entries[entry].artifact;
                        state->code_bytes = (u32)state->reused.code.length;
                    }
                    else
                    {
                        state->lookup = INCREMENTAL_LOOKUP_MISS_MALFORMED;
                    }
                }
            }
            session->statistics.lookups[state->lookup] += 1;
        }
        session->statistics.record_nanoseconds = timestamp_ns_between(start, timestamp_take());
        session->active = result;
    }
    return result;
}

void incremental_session_begin_attempt(IncrementalCodegenSession* session)
{
    if (session && session->active)
    {
        session->attempt_count += 1;
        if (session->attempt_count > 1)
        {
            // A retried attempt runs after module-level assembly of the
            // failed one may have defined or created symbols, so the records
            // built before the first attempt no longer describe its inputs.
            session->active = false;
            session->statistics.retry_disabled = true;
        }
    }
}

IncrementalFunctionArtifact const* incremental_session_reused(IncrementalCodegenSession* session, u32 function_index)
{
    IncrementalFunctionArtifact const* result = 0;
    if (session && session->active && !session->options.verify && function_index < session->function_count &&
        session->functions[function_index].lookup == INCREMENTAL_LOOKUP_REUSED)
    {
        result = &session->functions[function_index].reused;
    }
    return result;
}

IrSymbolId const* incremental_session_symbol_slots(IncrementalCodegenSession* session, u32 function_index, u32* slot_count)
{
    IrSymbolId const* result = 0;
    *slot_count = 0;
    if (session && session->active && function_index < session->function_count && session->functions[function_index].record.pointer)
    {
        result = session->functions[function_index].symbol_slots;
        *slot_count = session->functions[function_index].symbol_slot_count;
    }
    return result;
}

bool incremental_session_active(IncrementalCodegenSession* session)
{
    return session && session->active;
}

bool incremental_session_verifying(IncrementalCodegenSession* session)
{
    return session && session->active && session->options.verify;
}

void incremental_session_capture(IncrementalCodegenSession* session, u32 function_index, IncrementalCapture capture,
                                 IncrementalFunctionArtifact const* artifact)
{
    if (session && session->active && function_index < session->function_count)
    {
        IncrementalFunctionState* state = session->functions + function_index;
        bool eligible = state->record.pointer != 0;
        IncrementalCapture kind = eligible ? capture : INCREMENTAL_CAPTURE_NONE;
        if (kind == INCREMENTAL_CAPTURE_STORED)
        {
            state->captured_artifact = incremental_artifact_encode(session->arena, artifact);
            state->code_bytes = (u32)artifact->code.length;
            if (!state->captured_artifact.pointer)
            {
                kind = INCREMENTAL_CAPTURE_UNSUPPORTED;
            }
        }
        if (eligible && session->options.verify && state->lookup == INCREMENTAL_LOOKUP_REUSED)
        {
            session->statistics.verified += 1;
            bool same = kind == INCREMENTAL_CAPTURE_STORED && state->captured_artifact.length == state->stored_artifact.length &&
                        memory_compare(state->captured_artifact.pointer, state->stored_artifact.pointer, state->stored_artifact.length);
            if (!same)
            {
                state->verify_mismatch = true;
                session->statistics.verify_mismatches += 1;
            }
        }
        state->capture = kind;
        if (eligible)
        {
            session->statistics.captures[kind] += 1;
            session->statistics.compiled_ir_instructions += session->module->functions[function_index].instruction_count;
        }
    }
}

void incremental_session_note_replay(IncrementalCodegenSession* session, u32 function_index, u64 nanoseconds)
{
    if (session && function_index < session->function_count)
    {
        IncrementalFunctionArtifact const* artifact = &session->functions[function_index].reused;
        session->statistics.reused_ir_instructions += session->module->functions[function_index].instruction_count;
        session->statistics.reused_code_bytes += artifact->code.length;
        session->statistics.reused_relocations += artifact->relocation_count;
        session->statistics.reused_line_marks += artifact->line_mark_count;
        session->statistics.reused_debug_locations += artifact->debug_location_count;
        session->statistics.replay_nanoseconds += nanoseconds;
    }
}

bool incremental_session_publish(IncrementalCodegenSession* session)
{
    bool result = session && session->active && session->bound && session->pack_path.length;
    if (result)
    {
        TimeDataType start = timestamp_take();
        Arena* arena = session->arena;
        IncrementalPackEntry* entries = arena_allocate(arena, IncrementalPackEntry, session->function_count ? session->function_count : 1);
        IncrementalManifestEntry* manifest = arena_allocate(arena, IncrementalManifestEntry, session->function_count ? session->function_count : 1);
        u32 entry_count = 0;
        u32 manifest_count = 0;
        for (u32 index = 0; index < session->function_count; index += 1)
        {
            IncrementalFunctionState const* state = session->functions + index;
            if (state->record.pointer)
            {
                // A verified hit publishes what was compiled this time; an
                // unverified hit republishes the stored bytes it replayed.
                ByteSlice artifact = state->capture == INCREMENTAL_CAPTURE_STORED ? state->captured_artifact
                                     : state->lookup == INCREMENTAL_LOOKUP_REUSED && !session->options.verify ? state->stored_artifact
                                                                                                              : (ByteSlice){0};
                u32 entry_index = UINT32_MAX;
                if (artifact.pointer)
                {
                    entry_index = entry_count;
                    entries[entry_count] = (IncrementalPackEntry){.fingerprint = state->fingerprint, .record = state->record, .artifact = artifact};
                    entry_count += 1;
                }
                IncrementalManifestEntry* row = manifest + manifest_count;
                *row = (IncrementalManifestEntry){
                    .name = session->module->functions[index].name, .fingerprint = state->fingerprint, .entry_index = entry_index};
                for (u32 section = 0; section < INCREMENTAL_SECTION_COUNT; section += 1)
                {
                    row->sections[section] = state->sections[section];
                }
                manifest_count += 1;
            }
        }
        ByteSlice pack = incremental_pack_encode(arena, session->context, entries, entry_count, manifest, manifest_count);
        ByteSlice previous = session->previous_file.bytes;
        bool unchanged = pack.pointer && previous.pointer && previous.length == pack.length && memory_compare(previous.pointer, pack.pointer, pack.length);
        // Everything the new pack reuses has been copied into it, so the old
        // mapping can go before the rename replaces the file it maps.
        file_map_unmap(session->previous_file);
        session->previous_file = (FileMapRead){0};
        session->previous = (IncrementalPack){0};
        result = pack.pointer != 0 && (unchanged || incremental_publish_file(arena, session->options.cache_directory, session->pack_path, pack));
        if (result && !unchanged)
        {
            session->statistics.pack_bytes_written = pack.length;
            session->statistics.pack_entries_written = incremental_load_u32(pack.pointer + 36);
        }
        session->statistics.published = result;
        session->statistics.pack_write_nanoseconds = timestamp_ns_between(start, timestamp_take());
    }
    return result;
}

IncrementalStatistics incremental_session_statistics(IncrementalCodegenSession* session)
{
    IncrementalStatistics result = {0};
    if (session)
    {
        result = session->statistics;
        result.pack_statuses[result.pack_status] = 1;
        result.units_published = result.published;
    }
    return result;
}

void incremental_statistics_add(IncrementalStatistics* total, IncrementalStatistics const* unit)
{
    for (u32 index = 0; index < INCREMENTAL_LOOKUP_COUNT; index += 1)
    {
        total->lookups[index] += unit->lookups[index];
    }
    for (u32 index = 0; index < INCREMENTAL_CAPTURE_COUNT; index += 1)
    {
        total->captures[index] += unit->captures[index];
    }
    for (u32 index = 0; index < INCREMENTAL_PACK_STATUS_COUNT; index += 1)
    {
        total->pack_statuses[index] += unit->pack_statuses[index];
    }
    total->reused_ir_instructions += unit->reused_ir_instructions;
    total->reused_code_bytes += unit->reused_code_bytes;
    total->reused_relocations += unit->reused_relocations;
    total->reused_line_marks += unit->reused_line_marks;
    total->reused_debug_locations += unit->reused_debug_locations;
    total->compiled_ir_instructions += unit->compiled_ir_instructions;
    total->lowered_ir_instructions += unit->lowered_ir_instructions;
    total->record_bytes += unit->record_bytes;
    total->record_body_bytes += unit->record_body_bytes;
    total->record_type_bytes += unit->record_type_bytes;
    total->record_symbol_bytes += unit->record_symbol_bytes;
    total->record_types += unit->record_types;
    total->record_symbols += unit->record_symbols;
    total->pack_bytes_read += unit->pack_bytes_read;
    total->pack_bytes_written += unit->pack_bytes_written;
    total->pack_entries_read += unit->pack_entries_read;
    total->pack_entries_written += unit->pack_entries_written;
    total->identity_bytes = BUSTER_MAX(total->identity_bytes, unit->identity_bytes);
    total->verified += unit->verified;
    total->verify_mismatches += unit->verify_mismatches;
    total->audit_functions += unit->audit_functions;
    total->audit_touched_types += unit->audit_touched_types;
    total->audit_closure_types += unit->audit_closure_types;
    total->audit_touched_symbols += unit->audit_touched_symbols;
    total->audit_closure_symbols += unit->audit_closure_symbols;
    total->audit_type_violations += unit->audit_type_violations;
    total->audit_symbol_violations += unit->audit_symbol_violations;
    total->audit_overflows += unit->audit_overflows;
    total->identity_nanoseconds = BUSTER_MAX(total->identity_nanoseconds, unit->identity_nanoseconds);
    total->pack_read_nanoseconds += unit->pack_read_nanoseconds;
    total->record_nanoseconds += unit->record_nanoseconds;
    total->replay_nanoseconds += unit->replay_nanoseconds;
    total->pack_write_nanoseconds += unit->pack_write_nanoseconds;
    total->units_published += unit->units_published;
    total->pack_status = unit->pack_status;
    total->enabled = total->enabled || unit->enabled;
    total->published = total->published || unit->published;
    total->retry_disabled = total->retry_disabled || unit->retry_disabled;
}

IncrementalFunctionTrace* incremental_session_trace(IncrementalCodegenSession* session, Arena* arena, u32* count)
{
    IncrementalFunctionTrace* result = 0;
    *count = 0;
    if (session && session->options.trace && session->bound && session->function_count)
    {
        result = arena_allocate(arena, IncrementalFunctionTrace, session->function_count);
        for (u32 index = 0; index < session->function_count; index += 1)
        {
            IncrementalFunctionState const* state = session->functions + index;
            IrFunction const* function = session->module->functions + index;
            if (state->lookup != INCREMENTAL_LOOKUP_NOT_LOWERED)
            {
                result[*count] = (IncrementalFunctionTrace){
                    .name = string_duplicate_arena(arena, function->name, false),
                    .fingerprint = state->fingerprint,
                    .instruction_count = function->instruction_count,
                    .code_bytes = state->code_bytes,
                    .lookup = state->lookup,
                    .capture = state->capture,
                    .verify_mismatch = state->verify_mismatch,
                    .audit_violation = state->audit_violation,
                };
                *count += 1;
            }
        }
    }
    return result;
}

#if BUSTER_INCREMENTAL_AUDIT
// Reads are recorded in order with repeats; the check sorts them in place and
// counts each distinct id once.
#define INCREMENTAL_AUDIT_CAPACITY (1u << 22)

BUSTER_GLOBAL_LOCAL u32 incremental_audit_unique(u32* values, u32 count)
{
    IncrementalSortInput input = {.kind = INCREMENTAL_SORT_U32};
    incremental_sort_indices(&input, values, count);
    u32 unique = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        if (!unique || values[unique - 1] != values[index])
        {
            values[unique] = values[index];
            unique += 1;
        }
    }
    return unique;
}

BUSTER_GLOBAL_LOCAL bool incremental_audit_contains(u32 const* values, u32 count, u32 value)
{
    u32 low = 0;
    u32 high = count;
    bool result = false;
    while (low < high && !result)
    {
        u32 middle = low + (high - low) / 2;
        result = values[middle] == value;
        if (values[middle] < value)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return result;
}

void incremental_session_audit_open(IncrementalCodegenSession* session, u32 function_index)
{
    ir_access_audit_close();
    if (session && session->active && function_index < session->function_count && session->functions[function_index].record.pointer)
    {
        if (!session->audit.types)
        {
            session->audit.types = arena_allocate(session->arena, u32, INCREMENTAL_AUDIT_CAPACITY);
            session->audit.symbols = arena_allocate(session->arena, u32, INCREMENTAL_AUDIT_CAPACITY);
            session->audit.type_capacity = INCREMENTAL_AUDIT_CAPACITY;
            session->audit.symbol_capacity = INCREMENTAL_AUDIT_CAPACITY;
        }
        session->audit.type_count = 0;
        session->audit.symbol_count = 0;
        session->audit.overflow = false;
        ir_access_audit_open(&session->audit);
    }
}

void incremental_session_audit_close(IncrementalCodegenSession* session, u32 function_index)
{
    ir_access_audit_close();
    if (session && session->active && function_index < session->function_count && session->functions[function_index].record.pointer &&
        session->audit.types)
    {
        IncrementalFunctionState* state = session->functions + function_index;
        u32 types = incremental_audit_unique(session->audit.types, session->audit.type_count);
        u32 symbols = incremental_audit_unique(session->audit.symbols, session->audit.symbol_count);
        u32 touched_types = 0;
        u32 touched_symbols = 0;
        // Ids past the table resolve to nothing: the answer "no such entry" is
        // decided by the id itself, which the record carries.
        for (u32 index = 0; index < types; index += 1)
        {
            u32 id = session->audit.types[index];
            if (id < session->type_capacity)
            {
                touched_types += 1;
                if (!incremental_audit_contains(state->audit_types, state->audit_type_count, id))
                {
                    session->statistics.audit_type_violations += 1;
                    state->audit_violation = true;
                }
            }
        }
        for (u32 index = 0; index < symbols; index += 1)
        {
            u32 id = session->audit.symbols[index];
            if (id < session->symbol_capacity)
            {
                touched_symbols += 1;
                if (!incremental_audit_contains((u32 const*)state->symbol_slots, state->symbol_slot_count, id))
                {
                    session->statistics.audit_symbol_violations += 1;
                    state->audit_violation = true;
                }
            }
        }
        session->statistics.audit_functions += 1;
        session->statistics.audit_touched_types += touched_types;
        session->statistics.audit_closure_types += state->audit_type_count;
        session->statistics.audit_touched_symbols += touched_symbols;
        session->statistics.audit_closure_symbols += state->symbol_slot_count;
        session->statistics.audit_overflows += session->audit.overflow;
    }
}
#endif

void incremental_session_close(IncrementalCodegenSession* session)
{
#if BUSTER_INCREMENTAL_AUDIT
    ir_access_audit_close();
#endif
    if (session)
    {
        file_map_unmap(session->previous_file);
        arena_destroy(session->arena, 1);
    }
}
