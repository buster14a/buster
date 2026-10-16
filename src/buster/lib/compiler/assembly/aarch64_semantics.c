// Arm A64 semantic records retain the generated schema and source identities.
// Only their private numeric storage is packed: fixed record blocks are
// transposed by byte column, then encoded as bounded literal/run packets.
// buster_a64_semantic_cached_byte serves cold queries with once-published
// blocks; buster_a64_semantic_blob_decode is the pure descriptor fallback.
// buster_a64_semantic_blob_fill_block restores row bytes for each claim;
// buster_aarch64_semantics_prewarm publishes whole-table readiness last.
#include <buster/lib/compiler/assembly/aarch64_semantics.h>
#include <buster/lib/os.h>

#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif
#include <buster/lib/compiler/assembly/generated/arm-a64-semantic.generated.h>
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic pop
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

BUSTER_CT_CHECK(BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE ==
                BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT * BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_RECORD_BYTES);
BUSTER_CT_CHECK(BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE ==
                BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT * BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_RECORD_BYTES);

static bool
buster_a64_semantic_range(u32 first, u32 count, u32 total)
{
    return first <= total && count <= total - first;
}

static bool
buster_a64_semantic_string_offset(u32 offset, BusterA64SemanticString* result)
{
    if (!result || offset >= BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE) return false;
    u32 length = 0;
    while (offset + length < BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE &&
           buster_a64_semantic_string_pool[offset + length] != 0)
    {
        length += 1;
    }
    if (offset + length >= BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE) return false;
    *result = (BusterA64SemanticString){.offset = offset, .length = length};
    return true;
}

static u8 buster_a64_semantic_base64_value(char8 value)
{
    u8 result;
    if (value >= 'A' && value <= 'Z')
    {
        result = (u8)(value - 'A');
    }
    else if (value >= 'a' && value <= 'z')
    {
        result = (u8)(value - 'a' + 26);
    }
    else if (value >= '0' && value <= '9')
    {
        result = (u8)(value - '0' + 52);
    }
    else if (value == '+')
    {
        result = 62;
    }
    else if (value == '/')
    {
        result = 63;
    }
    else
    {
        result = 0;
    }

    return result;
}

// Packed storage changes neither the record wire format nor its hot readers.
// Serial prewarm restores the original little-endian row bytes in these
// caches; every warmed accessor remains a direct byte read. Cold accessors
// fill each fixed block once through the existing once-claim protocol.
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_segment_bytes[BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_field_bytes[BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_operand_bytes[BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_value_atom_bytes[BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_value_entry_bytes[BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE];
// Read last by every accessor and written last by the prewarm. While it is
// false, only individually published blocks may be read. A cold caller
// claims its block before filling; no other block or ready flag is written.
BUSTER_GLOBAL_LOCAL bool buster_a64_semantic_bytes_ready;

BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_base64_byte(char8 const* blob, u32 blob_size, u32 index)
{
    u8 result;
    if (index >= blob_size)
    {
        result = 0;
    }
    else
    {
        u32 group = index / 3;
        u32 within = index % 3;
        u32 encoded = group * 4;
        u32 value = ((u32)buster_a64_semantic_base64_value(blob[encoded]) << 18) |
                    ((u32)buster_a64_semantic_base64_value(blob[encoded + 1]) << 12) |
                    ((u32)buster_a64_semantic_base64_value(blob[encoded + 2]) << 6) |
                    (u32)buster_a64_semantic_base64_value(blob[encoded + 3]);
        result = (u8)((value >> (16 - 8 * within)) & 0xff);
    }

    return result;
}

BUSTER_CT_CHECK(BUSTER_AARCH64_SEMANTIC_STORAGE_VERSION == 1);

BUSTER_GLOBAL_LOCAL u32 buster_a64_semantic_packed_size(char8 const* encoded, u32 length)
{
    u32 result = length / 4 * 3;
    if (length)
    {
        result -= encoded[length - 1] == '=';
        result -= length > 1 && encoded[length - 2] == '=';
    }
    return result;
}

// Packet bounds are checked before any write. A complete block must consume
// its complete encoded payload; neither an overrun nor an ignored tail passes.
// Column packets write directly into row storage, including across packets.
BUSTER_GLOBAL_LOCAL bool buster_a64_semantic_unpack_block(char8 const* encoded, u32 length, u8* output, u32 output_count, u32 record_bytes)
{
    bool valid = (!length || encoded) && (!output_count || output) && length % 4 == 0 && record_bytes && output_count % record_bytes == 0;
    u32 packed_size = valid ? buster_a64_semantic_packed_size(encoded, length) : 0;
    u32 rows = valid ? output_count / record_bytes : 0;
    u32 input = 0;
    u32 written = 0;
    u32 row = 0;
    u32 column = 0;
    u32 destination = 0;
    while (valid && written < output_count)
    {
        valid = input < packed_size;
        if (valid)
        {
            u8 control = buster_a64_semantic_base64_byte(encoded, packed_size, input++);
            bool repeated = control >= 128;
            u32 count = repeated ? (u32)control - 128 + 3 : (u32)control + 1;
            valid = count <= output_count - written && (repeated ? input < packed_size : count <= packed_size - input);
            if (valid)
            {
                u8 value = repeated ? buster_a64_semantic_base64_byte(encoded, packed_size, input++) : 0;
                for (u32 index = 0; index < count; index += 1)
                {
                    output[destination] = repeated ? value : buster_a64_semantic_base64_byte(encoded, packed_size, input++);
                    written += 1;
                    row += 1;
                    if (row == rows)
                    {
                        row = 0;
                        column += 1;
                        destination = column;
                    }
                    else
                    {
                        destination += record_bytes;
                    }
                }
            }
        }
    }
    valid = valid && written == output_count && input == packed_size;
    return valid;
}

// A cold read scans only its bounded block and skips literal packet payloads
// it does not need. It never initializes or mutates shared storage.
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_blob_decode(BusterA64SemanticGeneratedBlob const* blob, u32 blob_size, u32 index)
{
    u8 result = 0;
    bool valid = blob && blob->data && blob->blocks && blob->record_bytes &&
                 blob->record_bytes <= BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_BYTES / BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS &&
                 blob_size % blob->record_bytes == 0 && index < blob_size;
    if (valid)
    {
        u32 block_bytes = blob->record_bytes * BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS;
        u32 block = index / block_bytes;
        u32 remaining = blob_size - block * block_bytes;
        u32 size = BUSTER_MIN(block_bytes, remaining);
        u32 rows = size / blob->record_bytes;
        u32 within = index % block_bytes;
        u32 wanted = (within % blob->record_bytes) * rows + within / blob->record_bytes;
        u32 first = blob->blocks[block];
        u32 end = blob->blocks[block + 1];
        valid = end >= first && (end - first) % 4 == 0;
        char8 const* encoded = blob->data + first;
        u32 packed_size = valid ? buster_a64_semantic_packed_size(encoded, end - first) : 0;
        u32 input = 0;
        u32 decoded = 0;
        bool found = false;
        while (valid && !found && decoded < size)
        {
            valid = input < packed_size;
            if (valid)
            {
                u8 control = buster_a64_semantic_base64_byte(encoded, packed_size, input++);
                bool repeated = control >= 128;
                u32 count = repeated ? (u32)control - 128 + 3 : (u32)control + 1;
                valid = count <= size - decoded && (repeated ? input < packed_size : count <= packed_size - input);
                if (valid)
                {
                    found = wanted - decoded < count;
                    if (found)
                    {
                        result = buster_a64_semantic_base64_byte(encoded, packed_size, input + (repeated ? 0 : wanted - decoded));
                    }
                    input += repeated ? 1 : count;
                    decoded += count;
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_semantic_blob_fill_block(BusterA64SemanticGeneratedBlob const* blob, u32 blob_size, u32 block_index, u8* bytes)
{
    bool valid = blob && blob->data && blob->blocks && blob->record_bytes &&
                 blob->record_bytes <= BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_BYTES / BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS &&
                 blob_size % blob->record_bytes == 0 && bytes;
    u32 block_bytes = valid ? blob->record_bytes * BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS : 0;
    valid = valid && block_index < blob_size / block_bytes + (blob_size % block_bytes != 0);
    if (valid)
    {
        u32 first = block_index * block_bytes;
        u32 encoded_first = blob->blocks[block_index];
        u32 encoded_end = blob->blocks[block_index + 1];
        u32 remaining = blob_size - first;
        u32 size = BUSTER_MIN(block_bytes, remaining);
        valid = encoded_end >= encoded_first && buster_a64_semantic_unpack_block(blob->data + encoded_first, encoded_end - encoded_first, bytes, size, blob->record_bytes);
    }
    return valid;
}

// Each state covers a disjoint decoded block. A fill reads only immutable
// generated bytes and claims no other entry, so its once level is a leaf.
// Both serial and lane-time readers publish exactly the same original bytes.
enum
{
    BUSTER_A64_SEMANTIC_BLOCK_PENDING,
    BUSTER_A64_SEMANTIC_BLOCK_BUSY,
    BUSTER_A64_SEMANTIC_BLOCK_READY,
    BUSTER_A64_SEMANTIC_BLOCK_INVALID,
    BUSTER_A64_SEMANTIC_ONCE_LEVEL = 1,
};
#define BUSTER_A64_SEMANTIC_BLOCK_COUNT(records) (((records) + BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS - 1) / BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS)
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_segment_states[BUSTER_A64_SEMANTIC_BLOCK_COUNT(BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT)];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_field_states[BUSTER_A64_SEMANTIC_BLOCK_COUNT(BUSTER_AARCH64_SEMANTIC_FIELD_COUNT)];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_operand_states[BUSTER_A64_SEMANTIC_BLOCK_COUNT(BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT)];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_value_atom_states[BUSTER_A64_SEMANTIC_BLOCK_COUNT(BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT)];
BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_value_entry_states[BUSTER_A64_SEMANTIC_BLOCK_COUNT(BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT)];
#undef BUSTER_A64_SEMANTIC_BLOCK_COUNT

typedef struct BusterA64SemanticBlobCache BusterA64SemanticBlobCache;
struct BusterA64SemanticBlobCache
{
    BusterA64SemanticGeneratedBlob const* blob;
    u8* bytes;
    u8* states;
    u32 size;
    u32 block_count;
};
BUSTER_GLOBAL_LOCAL BusterA64SemanticBlobCache const buster_a64_semantic_blob_caches[] = {
    {&buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, buster_a64_semantic_segment_states, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, BUSTER_ARRAY_LENGTH(buster_a64_semantic_segment_states)},
    {&buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, buster_a64_semantic_field_states, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, BUSTER_ARRAY_LENGTH(buster_a64_semantic_field_states)},
    {&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, buster_a64_semantic_operand_states, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, BUSTER_ARRAY_LENGTH(buster_a64_semantic_operand_states)},
    {&buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, buster_a64_semantic_value_atom_states, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, BUSTER_ARRAY_LENGTH(buster_a64_semantic_value_atom_states)},
    {&buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, buster_a64_semantic_value_entry_states, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, BUSTER_ARRAY_LENGTH(buster_a64_semantic_value_entry_states)},
};

BUSTER_GLOBAL_LOCAL bool buster_a64_semantic_ensure_block(BusterA64SemanticBlobCache const* cache, u32 block)
{
    bool valid = block < cache->block_count;
    if (valid)
    {
        u8* state = &cache->states[block];
        valid = atomic_u8_load_acquire(state) == BUSTER_A64_SEMANTIC_BLOCK_READY;
        if (!valid)
        {
            if (once_claim_u8(state, BUSTER_A64_SEMANTIC_BLOCK_PENDING, BUSTER_A64_SEMANTIC_BLOCK_BUSY, BUSTER_A64_SEMANTIC_ONCE_LEVEL))
            {
                u32 first = block * cache->blob->record_bytes * BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS;
                bool decoded = buster_a64_semantic_blob_fill_block(cache->blob, cache->size, block, cache->bytes + first);
                once_publish_u8(state, decoded ? BUSTER_A64_SEMANTIC_BLOCK_READY : BUSTER_A64_SEMANTIC_BLOCK_INVALID);
            }
            valid = atomic_u8_load_acquire(state) == BUSTER_A64_SEMANTIC_BLOCK_READY;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_cached_byte(BusterA64SemanticGeneratedBlob const* blob, u32 blob_size, u32 index)
{
    u8 result = 0;
    if (blob && index < blob_size)
    {
        // Never retain or publish data for a temporary descriptor. The five
        // generated identities and their original decoded sizes are fixed.
        if (blob->cache_slot < BUSTER_ARRAY_LENGTH(buster_a64_semantic_blob_caches) &&
            buster_a64_semantic_blob_caches[blob->cache_slot].blob == blob &&
            buster_a64_semantic_blob_caches[blob->cache_slot].size == blob_size)
        {
            BusterA64SemanticBlobCache const* cache = &buster_a64_semantic_blob_caches[blob->cache_slot];
            u32 block_bytes = blob->record_bytes * BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS;
            if (buster_a64_semantic_ensure_block(cache, index / block_bytes))
            {
                result = cache->bytes[index];
            }
        }
        else
        {
            result = buster_a64_semantic_blob_decode(blob, blob_size, index);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 buster_a64_semantic_blob_byte(BusterA64SemanticGeneratedBlob const* blob, u8 const* bytes, u32 blob_size, u32 index)
{
    u8 result;
    if (index >= blob_size)
    {
        result = 0;
    }
    else if (buster_a64_semantic_bytes_ready)
    {
        result = bytes[index];
    }
    else
    {
        result = buster_a64_semantic_cached_byte(blob, blob_size, index);
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool buster_a64_semantic_blob_fill(BusterA64SemanticGeneratedBlob const* blob, u8* bytes, u32 blob_size)
{
    bool valid = blob && blob->data && blob->blocks && blob->record_bytes &&
                 blob->record_bytes <= BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_BYTES / BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS &&
                 blob_size % blob->record_bytes == 0 && (!blob_size || bytes);
    u32 block_bytes = valid ? blob->record_bytes * BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS : 0;
    for (u32 first = 0; valid && first < blob_size; first += block_bytes)
    {
        valid = buster_a64_semantic_blob_fill_block(blob, blob_size, first / block_bytes, bytes + first);
    }
    return valid;
}
#endif

// Decode every semantic blob on the calling thread and publish the ready flag
// last. Complete prewarm remains a serial phase before `lane_run`; cold
// per-block fills use the same once-publication path without publishing it.
void buster_aarch64_semantics_prewarm(void)
{
    if (!buster_a64_semantic_bytes_ready)
    {
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        bool decoded = true;
        for (u32 table = 0; table < BUSTER_ARRAY_LENGTH(buster_a64_semantic_blob_caches); table += 1)
        {
            BusterA64SemanticBlobCache const* cache = &buster_a64_semantic_blob_caches[table];
            for (u32 block = 0; block < cache->block_count; block += 1)
            {
                decoded = buster_a64_semantic_ensure_block(cache, block) && decoded;
            }
        }
        BUSTER_RT_CHECK(decoded);
        buster_a64_semantic_bytes_ready = decoded;
    }
}

// Numeric readers acquire a complete span once, rather than repeating a
// block lookup for each byte. The checked fallback preserves partial-range
// zero bytes and pure reads of descriptors outside the generated caches.
BUSTER_GLOBAL_LOCAL u8 const* buster_a64_semantic_blob_span(BusterA64SemanticGeneratedBlob const* blob, u8 const* bytes, u32 blob_size, u32 offset, u32 count)
{
    u8 const* result = 0;
    if (count && offset < blob_size && count <= blob_size - offset)
    {
        bool ready = buster_a64_semantic_bytes_ready;
        if (!ready && blob && blob->cache_slot < BUSTER_ARRAY_LENGTH(buster_a64_semantic_blob_caches))
        {
            BusterA64SemanticBlobCache const* cache = &buster_a64_semantic_blob_caches[blob->cache_slot];
            if (cache->blob == blob && cache->bytes == bytes && cache->size == blob_size)
            {
                u32 block_bytes = blob->record_bytes * BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS;
                u32 first = offset / block_bytes;
                u32 last = (offset + count - 1) / block_bytes;
                ready = true;
                for (u32 block = first; block <= last; block += 1)
                {
                    ready = buster_a64_semantic_ensure_block(cache, block) && ready;
                }
            }
        }
        if (ready)
        {
            result = bytes + offset;
        }
    }
    return result;
}

static u16 buster_a64_semantic_blob_u16(BusterA64SemanticGeneratedBlob const* blob, u8 const* bytes, u32 blob_size, u32 offset)
{
    u8 const* span = buster_a64_semantic_blob_span(blob, bytes, blob_size, offset, 2);
    u16 result;
    if (span)
    {
        result = (u16)((u32)span[0] | (u32)span[1] << 8);
    }
    else
    {
        result = (u16)((u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset) |
                      (u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + 1) << 8);
    }
    return result;
}

static u32 buster_a64_semantic_blob_u32(BusterA64SemanticGeneratedBlob const* blob, u8 const* bytes, u32 blob_size, u32 offset)
{
    u8 const* span = buster_a64_semantic_blob_span(blob, bytes, blob_size, offset, 4);
    u32 result;
    if (span)
    {
        result = (u32)span[0] | (u32)span[1] << 8 | (u32)span[2] << 16 | (u32)span[3] << 24;
    }
    else
    {
        result = (u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset) |
                 (u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + 1) << 8 |
                 (u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + 2) << 16 |
                 (u32)buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + 3) << 24;
    }
    return result;
}

static u64 buster_a64_semantic_blob_u64(BusterA64SemanticGeneratedBlob const* blob, u8 const* bytes, u32 blob_size, u32 offset)
{
    u64 value = 0;
    u8 const* span = buster_a64_semantic_blob_span(blob, bytes, blob_size, offset, 8);
    for (u32 byte_index = 0; byte_index < 8; byte_index += 1)
    {
        u8 byte = span ? span[byte_index] : buster_a64_semantic_blob_byte(blob, bytes, blob_size, offset + byte_index);
        value |= (u64)byte << (8 * byte_index);
    }
    return value;
}

static bool buster_a64_semantic_generated_operand(u32 id, BusterA64SemanticGeneratedOperand* result)
{
    bool valid = result && id < BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT;
    if (valid)
    {
        u32 offset = id * BUSTER_AARCH64_SEMANTIC_OPERAND_RECORD_BYTES;
        *result = (BusterA64SemanticGeneratedOperand){
            .form_id = buster_a64_semantic_blob_u32(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 0),
            .link_offset = buster_a64_semantic_blob_u32(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 4),
            .symbol_offset = buster_a64_semantic_blob_u32(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 8),
            .field_first = buster_a64_semantic_blob_u32(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 12),
            .field_index_first = buster_a64_semantic_blob_u32(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 16),
            .transform_first = buster_a64_semantic_blob_u32(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 20),
            .kind_mask = buster_a64_semantic_blob_u32(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 24),
            .flags = buster_a64_semantic_blob_u64(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 28),
            .field_count = buster_a64_semantic_blob_u16(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 36),
            .field_index_count = buster_a64_semantic_blob_u16(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 38),
            .transform_count = buster_a64_semantic_blob_u16(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 40),
            .kind = buster_a64_semantic_blob_byte(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 42),
            .position = buster_a64_semantic_blob_byte(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 43),
            .classification_status = buster_a64_semantic_blob_byte(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 44),
            .reserved = buster_a64_semantic_blob_byte(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 45),
            .role_offset = buster_a64_semantic_blob_u32(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 46),
            .direction_offset = buster_a64_semantic_blob_u32(&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, offset + 50),
        };
    }

    return valid;
}

static bool buster_a64_semantic_generated_segment(u32 id, BusterA64SemanticGeneratedSegment* result)
{
    bool valid = result && id < BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT;
    if (valid)
    {
        u32 offset = id * BUSTER_AARCH64_SEMANTIC_SEGMENT_RECORD_BYTES;
        result->instruction_lsb = buster_a64_semantic_blob_byte(&buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 0);
        result->width = buster_a64_semantic_blob_byte(&buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 1);
        result->value_lsb = buster_a64_semantic_blob_byte(&buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 2);
        result->reserved = buster_a64_semantic_blob_byte(&buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, offset + 3);
    }

    return valid;
}

static bool buster_a64_semantic_generated_field(u32 id, BusterA64SemanticGeneratedField* result)
{
    bool valid = result && id < BUSTER_AARCH64_SEMANTIC_FIELD_COUNT;
    if (valid)
    {
        u32 offset = id * BUSTER_AARCH64_SEMANTIC_FIELD_RECORD_BYTES;
        result->name_offset = buster_a64_semantic_blob_u32(&buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 0);
        result->source_mask = buster_a64_semantic_blob_u32(&buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 4);
        result->segment_first = buster_a64_semantic_blob_u32(&buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 8);
        result->segment_count = buster_a64_semantic_blob_u16(&buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 12);
        result->width = buster_a64_semantic_blob_byte(&buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 14);
        result->reserved = buster_a64_semantic_blob_byte(&buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, offset + 15);
    }

    return valid;
}

static bool buster_a64_semantic_generated_value_atom(u32 id, BusterA64SemanticGeneratedValueAtom* result)
{
    bool valid = result && id < BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT;
    if (valid)
    {
        u32 offset = id * BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_RECORD_BYTES;
        result->text_offset = buster_a64_semantic_blob_u32(&buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 0);
        result->integer = (s64)buster_a64_semantic_blob_u64(&buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 4);
        result->program_first = buster_a64_semantic_blob_u32(&buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 12);
        result->program_count = buster_a64_semantic_blob_u16(&buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 16);
        result->type = buster_a64_semantic_blob_byte(&buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 18);
        result->reserved[0] = buster_a64_semantic_blob_byte(&buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, offset + 19);
    }

    return valid;
}

static bool buster_a64_semantic_generated_value_entry(u32 id, BusterA64SemanticGeneratedValueEntry* result)
{
    bool valid = result && id < BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT;
    if (valid)
    {
        u32 offset = id * BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_RECORD_BYTES;
        result->key_first = buster_a64_semantic_blob_u32(&buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, offset + 0);
        result->result_first = buster_a64_semantic_blob_u32(&buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, offset + 4);
        result->key_count = buster_a64_semantic_blob_u16(&buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, offset + 8);
        result->result_count = buster_a64_semantic_blob_u16(&buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, offset + 10);
    }

    return valid;
}

static bool buster_a64_semantic_generated_table_header(u32 id, BusterA64SemanticGeneratedTableHeader* result)
{
    bool valid = result && id < BUSTER_AARCH64_SEMANTIC_TABLE_COUNT;
    if (valid)
    {
        *result = buster_a64_semantic_table_headers[id];
    }

    return valid;
}

static bool buster_a64_semantic_generated_alias(u32 form_id, BusterA64SemanticGeneratedAlias* result)
{
    bool valid = result && form_id < BUSTER_AARCH64_SEMANTIC_FORM_COUNT;
    if (valid)
    {
        *result = buster_a64_semantic_aliases[form_id];
    }

    return valid;
}

static bool buster_a64_semantic_generated_constraint(u32 form_id, BusterA64SemanticGeneratedConstraint* result)
{
    bool valid = result && form_id < BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT;
    if (valid)
    {
        *result = buster_a64_semantic_constraints[form_id];
    }

    return valid;
}

u32 buster_a64_semantic_schema_version(void) { return BUSTER_AARCH64_SEMANTIC_SCHEMA_VERSION; }
u32 buster_a64_semantic_form_count(void) { return BUSTER_AARCH64_SEMANTIC_FORM_COUNT; }
u32 buster_a64_semantic_field_count(void) { return BUSTER_AARCH64_SEMANTIC_FIELD_COUNT; }
u32 buster_a64_semantic_segment_count(void) { return BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT; }
u32 buster_a64_semantic_operand_count(void) { return BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT; }
u32 buster_a64_semantic_operand_field_index_count(void) { return BUSTER_AARCH64_SEMANTIC_OPERAND_FIELD_INDEX_COUNT; }
u32 buster_a64_semantic_transform_count(void) { return BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT; }
u32 buster_a64_semantic_transform_part_count(void) { return BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT; }
u32 buster_a64_semantic_program_instruction_count(void) { return BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT; }
u32 buster_a64_semantic_program_operand_count(void) { return BUSTER_AARCH64_SEMANTIC_PROGRAM_OPERAND_COUNT; }
u32 buster_a64_semantic_parsed_program_count(void) { return BUSTER_AARCH64_SEMANTIC_PARSED_PROGRAM_COUNT; }
u32 buster_a64_semantic_value_program_count(void) { return BUSTER_AARCH64_SEMANTIC_VALUE_PROGRAM_COUNT; }
u32 buster_a64_semantic_value_count(void) { return BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT; }
u32 buster_a64_semantic_value_atom_count(void) { return BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT; }
u32 buster_a64_semantic_string_pool_size(void) { return BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE; }
u32 buster_a64_semantic_table_header_count(void) { return BUSTER_AARCH64_SEMANTIC_TABLE_COUNT; }
u32 buster_a64_semantic_table_key_header_count(void) { return BUSTER_AARCH64_SEMANTIC_TABLE_KEY_HEADER_COUNT; }
u32 buster_a64_semantic_alias_count(void) { return BUSTER_AARCH64_SEMANTIC_ALIAS_COUNT; }
u32 buster_a64_semantic_alias_condition_token_count(void) { return BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT; }
u32 buster_a64_semantic_alias_preference_condition_token_count(void) { return BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT; }
u32 buster_a64_semantic_alias_preference_count(void) { return BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT; }
u32 buster_a64_semantic_constraint_count(void) { return BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT; }
u32 buster_a64_semantic_constraint_feature_tag_count(void) { return BUSTER_AARCH64_SEMANTIC_CONSTRAINT_FEATURE_TAG_COUNT; }
u32 buster_a64_semantic_constraint_program_token_count(void) { return BUSTER_AARCH64_SEMANTIC_CONSTRAINT_PROGRAM_TOKEN_COUNT; }

bool buster_a64_semantic_string(u32 offset, BusterA64SemanticString* result)
{
    return buster_a64_semantic_string_offset(offset, result);
}

char8 buster_a64_semantic_string_byte(BusterA64SemanticString string, u32 index)
{
    char8 result;
    if (string.offset >= BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE || index >= string.length)
    {
        result = 0;
    }
    else if (index >= BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE - string.offset)
    {
        result = 0;
    }
    else
    {
        result = buster_a64_semantic_string_pool[string.offset + index];
    }

    return result;
}

bool buster_a64_semantic_form(u32 id, BusterA64SemanticForm* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT) return false;
    BusterA64SemanticGeneratedForm const* source = buster_a64_semantic_forms + id;
    BusterA64SemanticString name, mnemonic, assembly;
    if (!buster_a64_semantic_string_offset(source->name_offset, &name) ||
        !buster_a64_semantic_string_offset(source->mnemonic_offset, &mnemonic) ||
        !buster_a64_semantic_string_offset(source->assembly_offset, &assembly) ||
        !buster_a64_semantic_range(source->field_first, source->field_count, BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) ||
        !buster_a64_semantic_range(source->operand_first, source->operand_count, BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) ||
        !buster_a64_semantic_range(source->transform_first, source->transform_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT)) return false;
    *result = (BusterA64SemanticForm){
        .id = id, .name = name, .mnemonic = mnemonic, .assembly = assembly,
        .source_digest = source->source_digest, .fixed_mask = source->fixed_mask, .fixed_value = source->fixed_value,
        .field_first = source->field_first, .operand_first = source->operand_first, .transform_first = source->transform_first,
        .field_count = source->field_count, .operand_count = source->operand_count, .transform_count = source->transform_count,
        .owner = source->owner, .kind = source->kind, .raw_layout_resolved = source->raw_layout_resolved, .status = source->status,
    };
    return true;
}

bool buster_a64_semantic_field(u32 id, BusterA64SemanticField* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) return false;
    BusterA64SemanticGeneratedField source_value;
    if (!buster_a64_semantic_generated_field(id, &source_value)) return false;
    BusterA64SemanticGeneratedField const* source = &source_value;
    BusterA64SemanticString name;
    if (!buster_a64_semantic_string_offset(source->name_offset, &name) ||
        !buster_a64_semantic_range(source->segment_first, source->segment_count, BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT)) return false;
    *result = (BusterA64SemanticField){.id = id, .name = name, .source_mask = source->source_mask,
                                      .segment_first = source->segment_first, .segment_count = source->segment_count,
                                      .width = source->width};
    return true;
}

bool buster_a64_semantic_segment(u32 id, BusterA64SemanticSegment* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT) return false;
    BusterA64SemanticGeneratedSegment source_value;
    if (!buster_a64_semantic_generated_segment(id, &source_value)) return false;
    BusterA64SemanticGeneratedSegment const* source = &source_value;
    *result = (BusterA64SemanticSegment){.id = id, .instruction_lsb = source->instruction_lsb, .width = source->width,
                                         .value_lsb = source->value_lsb, .reserved = source->reserved};
    return true;
}

bool buster_a64_semantic_operand(u32 id, BusterA64SemanticOperand* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) return false;
    BusterA64SemanticGeneratedOperand source_value;
    if (!buster_a64_semantic_generated_operand(id, &source_value)) return false;
    BusterA64SemanticGeneratedOperand const* source = &source_value;
    BusterA64SemanticString link, symbol, role, direction;
    if (!buster_a64_semantic_string_offset(source->link_offset, &link) ||
        !buster_a64_semantic_string_offset(source->symbol_offset, &symbol) ||
        !buster_a64_semantic_string_offset(source->role_offset, &role) ||
        !buster_a64_semantic_string_offset(source->direction_offset, &direction) ||
        !buster_a64_semantic_range(source->field_index_first, source->field_index_count, BUSTER_AARCH64_SEMANTIC_OPERAND_FIELD_INDEX_COUNT) ||
        !buster_a64_semantic_range(source->transform_first, source->transform_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT)) return false;
    *result = (BusterA64SemanticOperand){
        .id = id, .form_id = source->form_id, .link = link, .symbol = symbol,
        .field_first = source->field_first, .field_indices_first = source->field_index_first, .transform_first = source->transform_first,
        .field_count = source->field_count, .field_index_count = source->field_index_count, .transform_count = source->transform_count,
        .kind = source->kind, .kind_mask = source->kind_mask, .flags = source->flags, .position = source->position,
        .classification_status = source->classification_status,
        .role = role, .direction = direction,
    };
    return true;
}

bool buster_a64_semantic_operand_field_index(u32 operand_id, u32 ordinal, u32* field_id)
{
    if (!field_id || operand_id >= BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) return false;
    BusterA64SemanticGeneratedOperand operand_value;
    if (!buster_a64_semantic_generated_operand(operand_id, &operand_value)) return false;
    BusterA64SemanticGeneratedOperand const* operand = &operand_value;
    if (ordinal >= operand->field_index_count || !buster_a64_semantic_range(operand->field_index_first, operand->field_index_count, BUSTER_AARCH64_SEMANTIC_OPERAND_FIELD_INDEX_COUNT)) return false;
    u32 value = buster_a64_semantic_operand_field_indices[operand->field_index_first + ordinal];
    if (value >= BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) return false;
    *field_id = value;
    return true;
}

static bool buster_a64_semantic_program_string(u32 offset, BusterA64SemanticString* result, bool require_nonempty)
{
    if (!buster_a64_semantic_string_offset(offset, result)) return false;
    return !require_nonempty || result->length != 0;
}

static bool buster_a64_semantic_program_slice(u16 high, u16 low, u16 width)
{
    bool result;
    if ((high == UINT16_MAX) != (low == UINT16_MAX))
    {
        result = false;
    }
    else if (high == UINT16_MAX)
    {
        result = true;
    }
    else
    {
        result = high < 64 && low <= high && width == (u16)(high - low + 1);
    }

    return result;
}

static bool buster_a64_semantic_validate_program_operand(BusterA64SemanticGeneratedProgramOperand const* operand)
{
    if (!operand || operand->kind > BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_LITERAL ||
        !buster_a64_semantic_program_slice(operand->high, operand->low, operand->width) || operand->reserved != 0) return false;
    BusterA64SemanticString ignored;
    bool result;
    if (!buster_a64_semantic_string_offset(operand->text_offset, &ignored))
    {
        result = false;
    }
    else if (operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD)
    {
        result = buster_a64_semantic_program_string(operand->field_offset, &ignored, true) && (operand->high != UINT16_MAX || operand->width == 0);
    }
    else if (operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_ARRANGEMENT)
    {
        result = operand->high == UINT16_MAX && operand->low == UINT16_MAX && operand->width == 0 && operand->field_offset == UINT32_MAX && ignored.length != 0;
    }
    else
    {
        result = operand->high == UINT16_MAX && operand->low == UINT16_MAX && operand->width == 0 && operand->field_offset == UINT32_MAX;
    }

    return result;
}

static bool buster_a64_semantic_validate_program_instruction(u32 id)
{
    if (id >= BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT) return false;
    BusterA64SemanticGeneratedProgramInstruction const* instruction = buster_a64_semantic_program_instructions + id;
    if (instruction->op > BUSTER_A64_SEMANTIC_PROGRAM_SHARED_DECODE || instruction->reserved[0] != 0 || instruction->reserved[1] != 0 || instruction->reserved[2] != 0 ||
        !buster_a64_semantic_range(instruction->operand_first, instruction->operand_count, BUSTER_AARCH64_SEMANTIC_PROGRAM_OPERAND_COUNT) ||
        !buster_a64_semantic_program_slice(instruction->high, instruction->low, instruction->width)) return false;
    BusterA64SemanticString ignored;
    if (!buster_a64_semantic_string_offset(instruction->text_offset, &ignored)) return false;
    bool field_op = instruction->op == BUSTER_A64_SEMANTIC_PROGRAM_FIELD || instruction->op == BUSTER_A64_SEMANTIC_PROGRAM_REGISTER_ADD_MOD;
    if (field_op)
    {
        if (!buster_a64_semantic_program_string(instruction->field_offset, &ignored, true)) return false;
    }
    else if (instruction->field_offset != UINT32_MAX) return false;
    switch (instruction->op)
    {
        case BUSTER_A64_SEMANTIC_PROGRAM_FIELD:
            return instruction->operand_count == 0 && instruction->value == 0 && instruction->modulus == 0 &&
                   (instruction->high != UINT16_MAX || instruction->width == 0);
        case BUSTER_A64_SEMANTIC_PROGRAM_UINT_CONCAT:
            if (instruction->operand_count == 0) return false;
            for (u32 index = 0; index < instruction->operand_count; index += 1)
            {
                BusterA64SemanticGeneratedProgramOperand const* operand = buster_a64_semantic_program_operands + instruction->operand_first + index;
                if (!buster_a64_semantic_validate_program_operand(operand) || operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_ARRANGEMENT) return false;
            }
            return instruction->width == 0 && instruction->value == 0 && instruction->modulus == 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_SIGN_EXTEND:
            return instruction->operand_count == 0 && instruction->high == UINT16_MAX && instruction->low == UINT16_MAX && instruction->width > 0 && instruction->width <= 64 && instruction->value == 0 && instruction->modulus == 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_SCALE_MUL:
        case BUSTER_A64_SEMANTIC_PROGRAM_SCALE_DIV:
            return instruction->operand_count == 0 && instruction->width == 0 && instruction->modulus == 0 && instruction->value > 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_SCALE_POW2:
        case BUSTER_A64_SEMANTIC_PROGRAM_ADD_CONST:
        case BUSTER_A64_SEMANTIC_PROGRAM_SUB_FROM_CONST:
        case BUSTER_A64_SEMANTIC_PROGRAM_LITERAL:
            return instruction->operand_count == 0 && instruction->width == 0 && instruction->modulus == 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_REGISTER_ADD_MOD:
            return instruction->operand_count == 0 && instruction->high == UINT16_MAX && instruction->low == UINT16_MAX && instruction->width == 0 && instruction->modulus == 32 && instruction->value > 0 && instruction->value <= 3;
        case BUSTER_A64_SEMANTIC_PROGRAM_TEXT_FACTOR:
            return instruction->operand_count == 0 && instruction->text_offset != 0 && ignored.length != 0 && instruction->width == 0 && instruction->value == 0 && instruction->modulus == 0;
        case BUSTER_A64_SEMANTIC_PROGRAM_SHARED_DECODE:
            if (instruction->operand_count == 0 || instruction->width != 0 || instruction->value != 0 || instruction->modulus != 0) return false;
            bool saw_field = false;
            bool saw_arrangement = false;
            for (u32 index = 0; index < instruction->operand_count; index += 1)
            {
                BusterA64SemanticGeneratedProgramOperand const* operand = buster_a64_semantic_program_operands + instruction->operand_first + index;
                if (!buster_a64_semantic_validate_program_operand(operand) || operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_LITERAL) return false;
                saw_field = saw_field || operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD;
                saw_arrangement = saw_arrangement || operand->kind == BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_ARRANGEMENT;
            }
            return saw_field && saw_arrangement;
    }
    return false;
}

static bool buster_a64_semantic_program_span(u32 first, u32 count)
{
    return count == 0 ? first == UINT32_MAX : buster_a64_semantic_range(first, count, BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT);
}

bool buster_a64_semantic_program_instruction(u32 id, BusterA64SemanticProgramInstruction* result)
{
    if (!result || !buster_a64_semantic_validate_program_instruction(id)) return false;
    BusterA64SemanticGeneratedProgramInstruction const* source = buster_a64_semantic_program_instructions + id;
    BusterA64SemanticString field = {0}, text = {0};
    if (!buster_a64_semantic_string_offset(source->text_offset, &text) ||
        (source->field_offset != UINT32_MAX && !buster_a64_semantic_string_offset(source->field_offset, &field))) return false;
    if (source->field_offset == UINT32_MAX) field = (BusterA64SemanticString){0};
    *result = (BusterA64SemanticProgramInstruction){.id = id, .field = field, .text = text, .operand_first = source->operand_first,
                                                    .operand_count = source->operand_count, .value = source->value, .high = source->high,
                                                    .low = source->low, .width = source->width, .modulus = source->modulus, .op = source->op};
    return true;
}

bool buster_a64_semantic_program_operand(u32 id, u32 ordinal, BusterA64SemanticProgramOperand* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT || !buster_a64_semantic_validate_program_instruction(id)) return false;
    BusterA64SemanticGeneratedProgramInstruction const* instruction = buster_a64_semantic_program_instructions + id;
    if (ordinal >= instruction->operand_count) return false;
    u32 operand_id = instruction->operand_first + ordinal;
    BusterA64SemanticGeneratedProgramOperand const* source = buster_a64_semantic_program_operands + operand_id;
    BusterA64SemanticString field = {0}, text = {0};
    if (!buster_a64_semantic_validate_program_operand(source) || !buster_a64_semantic_string_offset(source->text_offset, &text)) return false;
    if (source->field_offset != UINT32_MAX && !buster_a64_semantic_string_offset(source->field_offset, &field)) return false;
    *result = (BusterA64SemanticProgramOperand){.id = operand_id, .field = field, .text = text, .value = source->value,
                                                .high = source->high, .low = source->low, .width = source->width, .kind = source->kind};
    return true;
}

bool buster_a64_semantic_transform_program_instruction(u32 transform_id, u32 ordinal, BusterA64SemanticProgramInstruction* result)
{
    if (!result || transform_id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + transform_id;
    if (ordinal >= transform->program_count || !buster_a64_semantic_program_span(transform->program_first, transform->program_count)) return false;
    return buster_a64_semantic_program_instruction(transform->program_first + ordinal, result);
}

bool buster_a64_semantic_transform(u32 id, BusterA64SemanticTransform* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* source = buster_a64_semantic_transforms + id;
    BusterA64SemanticString expression;
    if (!buster_a64_semantic_string_offset(source->expression_offset, &expression)) return false;
    if (source->kind == BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE && source->table_id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    if (source->kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE && source->table_id != 0xffffffffu) return false;
    if (!buster_a64_semantic_program_span(source->program_first, source->program_count)) return false;
    if (!buster_a64_semantic_range(source->part_first, source->part_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT) ||
        !buster_a64_semantic_range(source->value_first, source->value_count, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT)) return false;
    *result = (BusterA64SemanticTransform){.id = id, .expression = expression, .source = source->source,
                                           .p0 = source->p0, .p1 = source->p1, .table_id = source->table_id, .program_first = source->program_first,
                                           .part_first = source->part_first, .value_first = source->value_first,
                                           .program_count = source->program_count, .part_count = source->part_count, .value_count = source->value_count, .kind = source->kind,
                                           .invertible = source->invertible != 0, .reserved = source->reserved};
    return true;
}

bool buster_a64_semantic_transform_part(u32 id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + id;
    if (ordinal >= transform->part_count || !buster_a64_semantic_range(transform->part_first, transform->part_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_transform_parts[transform->part_first + ordinal].offset, result);
}

bool buster_a64_semantic_transform_value(u32 id, u32 ordinal, BusterA64SemanticValue* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + id;
    if (ordinal >= transform->value_count || !buster_a64_semantic_range(transform->value_first, transform->value_count, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT)) return false;
    BusterA64SemanticGeneratedValueEntry value = {0};
    if (!buster_a64_semantic_generated_value_entry(transform->value_first + ordinal, &value) ||
        !buster_a64_semantic_range(value.key_first, value.key_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) ||
        !buster_a64_semantic_range(value.result_first, value.result_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) || value.result_count != 1) return false;
    *result = (BusterA64SemanticValue){.id = transform->value_first + ordinal, .key_first = value.key_first, .result_first = value.result_first, .key_count = value.key_count, .result_count = value.result_count};
    return true;
}

bool buster_a64_semantic_value_atom(u32 id, BusterA64SemanticValueAtom* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) return false;
    BusterA64SemanticGeneratedValueAtom atom = {0};
    if (!buster_a64_semantic_generated_value_atom(id, &atom)) return false;
    BusterA64SemanticString text;
    bool string_ok = buster_a64_semantic_string_offset(atom.text_offset, &text);
    bool type_ok = atom.type <= BUSTER_A64_SEMANTIC_VALUE_PROGRAM;
    bool span_ok = buster_a64_semantic_program_span(atom.program_first, atom.program_count);
    bool kind_span_ok = atom.type == BUSTER_A64_SEMANTIC_VALUE_PROGRAM ? atom.program_count != 0 : atom.program_count == 0;
    if (!string_ok || !type_ok || !span_ok || !kind_span_ok)
        return false;
    *result = (BusterA64SemanticValueAtom){.id = id, .kind = atom.type, .integer = atom.integer, .text = text,
                                           .program_first = atom.program_first, .program_count = atom.program_count};
    return true;
}

bool buster_a64_semantic_value_atom_program_instruction(u32 atom_id, u32 ordinal, BusterA64SemanticProgramInstruction* result)
{
    if (!result || atom_id >= BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) return false;
    BusterA64SemanticGeneratedValueAtom atom = {0};
    if (!buster_a64_semantic_generated_value_atom(atom_id, &atom) || atom.type != BUSTER_A64_SEMANTIC_VALUE_PROGRAM || ordinal >= atom.program_count ||
        !buster_a64_semantic_program_span(atom.program_first, atom.program_count)) return false;
    return buster_a64_semantic_program_instruction(atom.program_first + ordinal, result);
}

bool buster_a64_semantic_table_header(u32 id, BusterA64SemanticTableHeader* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    BusterA64SemanticGeneratedTableHeader source;
    if (!buster_a64_semantic_generated_table_header(id, &source) ||
        !buster_a64_semantic_range(source.key_header_first, source.key_header_count, BUSTER_AARCH64_SEMANTIC_TABLE_KEY_HEADER_COUNT)) return false;
    BusterA64SemanticString result_header;
    if (!buster_a64_semantic_string_offset(source.result_header_offset, &result_header)) return false;
    *result = (BusterA64SemanticTableHeader){.id = id, .key_header_first = source.key_header_first,
                                              .key_header_count = source.key_header_count, .result_header = result_header};
    return true;
}

bool buster_a64_semantic_table_key_header(u32 id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    BusterA64SemanticGeneratedTableHeader source;
    if (!buster_a64_semantic_generated_table_header(id, &source) || ordinal >= source.key_header_count ||
        !buster_a64_semantic_range(source.key_header_first, source.key_header_count, BUSTER_AARCH64_SEMANTIC_TABLE_KEY_HEADER_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_table_key_headers[source.key_header_first + ordinal], result);
}

bool buster_a64_semantic_table_result_header(u32 id, BusterA64SemanticString* result)
{
    if (!result || id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    BusterA64SemanticGeneratedTableHeader source;
    if (!buster_a64_semantic_generated_table_header(id, &source)) return false;
    return buster_a64_semantic_string_offset(source.result_header_offset, result);
}

bool buster_a64_semantic_transform_table_header(u32 transform_id, u32* table_id)
{
    if (!table_id || transform_id >= BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT) return false;
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + transform_id;
    if (transform->kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE || transform->table_id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT) return false;
    *table_id = transform->table_id;
    return true;
}

static bool buster_a64_semantic_alias_source(u32 form_id, BusterA64SemanticAlias* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT) return false;
    BusterA64SemanticGeneratedAlias source;
    if (!buster_a64_semantic_generated_alias(form_id, &source) ||
        !buster_a64_semantic_range(source.condition_first, source.condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT) ||
        !buster_a64_semantic_range(source.preference_condition_first, source.preference_condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT) ||
        !buster_a64_semantic_range(source.preference_first, source.preference_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT)) return false;
    BusterA64SemanticString target_file, target_id, target_encoding_id;
    if (!buster_a64_semantic_string_offset(source.target_file_offset, &target_file) ||
        !buster_a64_semantic_string_offset(source.target_id_offset, &target_id) ||
        !buster_a64_semantic_string_offset(source.target_encoding_id_offset, &target_encoding_id)) return false;
    *result = (BusterA64SemanticAlias){.form_id = form_id, .target_file = target_file, .target_id = target_id,
                                       .target_encoding_id = target_encoding_id, .condition_first = source.condition_first,
                                       .preference_condition_first = source.preference_condition_first, .preference_first = source.preference_first,
                                       .condition_count = source.condition_count, .preference_condition_count = source.preference_condition_count,
                                       .preference_count = source.preference_count, .preference_rank = source.preference_rank};
    return true;
}

bool buster_a64_semantic_alias(u32 form_id, BusterA64SemanticAlias* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT || buster_a64_semantic_forms[form_id].kind != BUSTER_A64_SEMANTIC_FORM_ALIAS) return false;
    return buster_a64_semantic_alias_source(form_id, result);
}

bool buster_a64_semantic_alias_descriptor(u32 form_id, BusterA64SemanticAlias* result)
{
    return buster_a64_semantic_alias_source(form_id, result);
}

bool buster_a64_semantic_alias_by_ordinal(u32 ordinal, BusterA64SemanticAlias* result)
{
    if (!result || ordinal >= BUSTER_AARCH64_SEMANTIC_ALIAS_COUNT) return false;
    u32 form_id = buster_a64_semantic_alias_form_ids[ordinal];
    return buster_a64_semantic_alias(form_id, result);
}

bool buster_a64_semantic_alias_condition_token(u32 form_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT || buster_a64_semantic_forms[form_id].kind != BUSTER_A64_SEMANTIC_FORM_ALIAS) return false;
    BusterA64SemanticGeneratedAlias source;
    if (!buster_a64_semantic_generated_alias(form_id, &source) || ordinal >= source.condition_count ||
        !buster_a64_semantic_range(source.condition_first, source.condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_alias_condition_tokens[source.condition_first + ordinal], result);
}

bool buster_a64_semantic_alias_preference_condition_token(u32 form_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT) return false;
    BusterA64SemanticGeneratedAlias source;
    if (!buster_a64_semantic_generated_alias(form_id, &source) || ordinal >= source.preference_condition_count ||
        !buster_a64_semantic_range(source.preference_condition_first, source.preference_condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_alias_preference_condition_tokens[source.preference_condition_first + ordinal], result);
}

bool buster_a64_semantic_alias_preference(u32 form_id, u32 ordinal, BusterA64SemanticAliasPreference* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_FORM_COUNT) return false;
    BusterA64SemanticGeneratedAlias alias_source;
    if (!buster_a64_semantic_generated_alias(form_id, &alias_source) || ordinal >= alias_source.preference_count ||
        !buster_a64_semantic_range(alias_source.preference_first, alias_source.preference_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT)) return false;
    BusterA64SemanticGeneratedAliasPreference const* source = buster_a64_semantic_alias_preferences + alias_source.preference_first + ordinal;
    if (!buster_a64_semantic_range(source->condition_first, source->condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT)) return false;
    BusterA64SemanticString alias_file, alias_id;
    if (!buster_a64_semantic_string_offset(source->alias_file_offset, &alias_file) || !buster_a64_semantic_string_offset(source->alias_id_offset, &alias_id)) return false;
    *result = (BusterA64SemanticAliasPreference){.id = alias_source.preference_first + ordinal, .alias_file = alias_file, .alias_id = alias_id,
                                                  .condition_first = source->condition_first, .condition_count = source->condition_count, .rank = source->rank};
    return true;
}

bool buster_a64_semantic_alias_preference_condition_token_by_id(u32 preference_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || preference_id >= BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT) return false;
    BusterA64SemanticGeneratedAliasPreference const* preference = buster_a64_semantic_alias_preferences + preference_id;
    if (ordinal >= preference->condition_count || !buster_a64_semantic_range(preference->condition_first, preference->condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_alias_preference_condition_tokens[preference->condition_first + ordinal], result);
}

bool buster_a64_semantic_constraint(u32 form_id, BusterA64SemanticConstraint* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT) return false;
    BusterA64SemanticGeneratedConstraint source;
    if (!buster_a64_semantic_generated_constraint(form_id, &source) ||
        !buster_a64_semantic_range(source.feature_first, source.feature_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_FEATURE_TAG_COUNT) ||
        !buster_a64_semantic_range(source.program_first, source.program_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_PROGRAM_TOKEN_COUNT)) return false;
    *result = (BusterA64SemanticConstraint){.form_id = form_id, .feature_first = source.feature_first, .program_first = source.program_first,
                                            .feature_count = source.feature_count, .program_count = source.program_count};
    return true;
}

bool buster_a64_semantic_constraint_feature_tag(u32 form_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT) return false;
    BusterA64SemanticGeneratedConstraint source;
    if (!buster_a64_semantic_generated_constraint(form_id, &source) || ordinal >= source.feature_count ||
        !buster_a64_semantic_range(source.feature_first, source.feature_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_FEATURE_TAG_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_constraint_feature_tags[source.feature_first + ordinal], result);
}

bool buster_a64_semantic_constraint_program_token(u32 form_id, u32 ordinal, BusterA64SemanticString* result)
{
    if (!result || form_id >= BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT) return false;
    BusterA64SemanticGeneratedConstraint source;
    if (!buster_a64_semantic_generated_constraint(form_id, &source) || ordinal >= source.program_count ||
        !buster_a64_semantic_range(source.program_first, source.program_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_PROGRAM_TOKEN_COUNT)) return false;
    return buster_a64_semantic_string_offset(buster_a64_semantic_constraint_program_tokens[source.program_first + ordinal], result);
}

static bool buster_a64_semantic_string_equals(BusterA64SemanticString actual, String8 expected)
{
    if (actual.length != expected.length) return false;
    for (u32 index = 0; index < actual.length; index += 1)
    {
        if (buster_a64_semantic_string_pool[actual.offset + index] != expected.pointer[index]) return false;
    }
    return true;
}

bool buster_a64_semantic_find_form(String8 name, u32 ordinal, u32* id)
{
    if (id && (name.pointer || !name.length))
    {
        u32 found = 0;
        for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_FORM_COUNT; index += 1)
        {
            BusterA64SemanticString candidate;
            if (buster_a64_semantic_string_offset(buster_a64_semantic_forms[index].name_offset, &candidate) &&
                buster_a64_semantic_string_equals(candidate, name))
            {
                if (found == ordinal) { *id = index; return true; }
                found += 1;
            }
        }
    }

    return false;
}

bool buster_a64_semantic_find_mnemonic(String8 value, u32 ordinal, u32* id)
{
    if (id && (value.pointer || !value.length))
    {
        u32 found = 0;
        for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_FORM_COUNT; index += 1)
        {
            BusterA64SemanticString candidate;
            if (buster_a64_semantic_string_offset(buster_a64_semantic_forms[index].mnemonic_offset, &candidate) &&
                buster_a64_semantic_string_equals(candidate, value))
            {
                if (found == ordinal) { *id = index; return true; }
                found += 1;
            }
        }
    }

    return false;
}

static bool buster_a64_semantic_validate_segment(BusterA64SemanticGeneratedSegment const* segment)
{
    return segment->width > 0 && segment->width <= 32 && segment->instruction_lsb < 32 &&
           segment->value_lsb < 32 && segment->instruction_lsb + segment->width <= 32 && segment->value_lsb + segment->width <= 32;
}

static bool buster_a64_semantic_validate_transform(u32 index)
{
    BusterA64SemanticGeneratedTransform const* transform = buster_a64_semantic_transforms + index;
    BusterA64SemanticString expression;
    if (transform->kind > BUSTER_A64_SEMANTIC_TRANSFORM_SHARED_DECODE || !buster_a64_semantic_string_offset(transform->expression_offset, &expression) ||
        !buster_a64_semantic_program_span(transform->program_first, transform->program_count) ||
        !buster_a64_semantic_range(transform->part_first, transform->part_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT) ||
        !buster_a64_semantic_range(transform->value_first, transform->value_count, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT) ||
        (transform->kind == BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE ? transform->table_id >= BUSTER_AARCH64_SEMANTIC_TABLE_COUNT : transform->table_id != 0xffffffffu)) return false;
    for (u32 part = 0; part < transform->part_count; part += 1)
    {
        BusterA64SemanticString ignored;
        if (!buster_a64_semantic_string_offset(buster_a64_semantic_transform_parts[transform->part_first + part].offset, &ignored)) return false;
    }
    for (u32 value_index = 0; value_index < transform->value_count; value_index += 1)
    {
        BusterA64SemanticGeneratedValueEntry value = {0};
        if (!buster_a64_semantic_generated_value_entry(transform->value_first + value_index, &value) || value.key_count == 0 || value.result_count != 1 ||
            !buster_a64_semantic_range(value.key_first, value.key_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT) ||
            !buster_a64_semantic_range(value.result_first, value.result_count, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT)) return false;
        for (u32 atom_index = 0; atom_index < value.key_count; atom_index += 1)
        {
            BusterA64SemanticGeneratedValueAtom atom = {0};
            if (!buster_a64_semantic_generated_value_atom(value.key_first + atom_index, &atom)) return false;
            BusterA64SemanticString ignored;
            if (atom.type > BUSTER_A64_SEMANTIC_VALUE_PROGRAM || !buster_a64_semantic_string_offset(atom.text_offset, &ignored) ||
                !buster_a64_semantic_program_span(atom.program_first, atom.program_count) ||
                (atom.type == BUSTER_A64_SEMANTIC_VALUE_PROGRAM ? atom.program_count == 0 : atom.program_count != 0)) return false;
        }
        BusterA64SemanticGeneratedValueAtom result_atom = {0};
        if (!buster_a64_semantic_generated_value_atom(value.result_first, &result_atom)) return false;
        BusterA64SemanticString ignored;
        if (result_atom.type > BUSTER_A64_SEMANTIC_VALUE_PROGRAM || !buster_a64_semantic_string_offset(result_atom.text_offset, &ignored) ||
            !buster_a64_semantic_program_span(result_atom.program_first, result_atom.program_count) ||
            (result_atom.type == BUSTER_A64_SEMANTIC_VALUE_PROGRAM ? result_atom.program_count == 0 : result_atom.program_count != 0)) return false;
    }
    return true;
}

bool buster_a64_semantic_validate(void)
{
    u32 owner_counts[BUSTER_A64_SEMANTIC_OWNER_COUNT] = {0};
    u32 undefined_count = 0;
    bool valid = true;
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT; index += 1)
    {
        BusterA64SemanticGeneratedSegment segment = {0};
        valid = valid && buster_a64_semantic_generated_segment(index, &segment) && buster_a64_semantic_validate_segment(&segment);
    }
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_PROGRAM_OPERAND_COUNT; index += 1)
    {
        valid = valid && buster_a64_semantic_validate_program_operand(buster_a64_semantic_program_operands + index);
    }
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT; index += 1)
    {
        valid = valid && buster_a64_semantic_validate_program_instruction(index);
    }
    u32 transform_index = 0;
    for (; transform_index < BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT; transform_index += 1)
    {
        valid = valid && buster_a64_semantic_validate_transform(transform_index);
    }
    for (u32 table_id = 0; table_id < BUSTER_AARCH64_SEMANTIC_TABLE_COUNT; table_id += 1)
    {
        BusterA64SemanticGeneratedTableHeader const* table = buster_a64_semantic_table_headers + table_id;
        valid = valid && table->key_header_count > 0 && buster_a64_semantic_range(table->key_header_first, table->key_header_count, BUSTER_AARCH64_SEMANTIC_TABLE_KEY_HEADER_COUNT);
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_string_offset(table->result_header_offset, &ignored);
        for (u32 header = 0; header < table->key_header_count; header += 1)
        {
            valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_table_key_headers[table->key_header_first + header], &ignored);
        }
    }
    for (u32 token_id = 0; token_id < BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT; token_id += 1)
    {
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_alias_condition_tokens[token_id], &ignored);
    }
    for (u32 token_id = 0; token_id < BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT; token_id += 1)
    {
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_alias_preference_condition_tokens[token_id], &ignored);
    }
    for (u32 form_id = 0; form_id < BUSTER_AARCH64_SEMANTIC_FORM_COUNT; form_id += 1)
    {
        BusterA64SemanticGeneratedAlias const* alias = buster_a64_semantic_aliases + form_id;
        valid = valid && buster_a64_semantic_range(alias->condition_first, alias->condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT) &&
                buster_a64_semantic_range(alias->preference_condition_first, alias->preference_condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT) &&
                buster_a64_semantic_range(alias->preference_first, alias->preference_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT);
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_string_offset(alias->target_file_offset, &ignored) && buster_a64_semantic_string_offset(alias->target_id_offset, &ignored) && buster_a64_semantic_string_offset(alias->target_encoding_id_offset, &ignored);
        BusterA64SemanticGeneratedConstraint const* constraint = buster_a64_semantic_constraints + form_id;
        valid = valid && buster_a64_semantic_range(constraint->feature_first, constraint->feature_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_FEATURE_TAG_COUNT) &&
                buster_a64_semantic_range(constraint->program_first, constraint->program_count, BUSTER_AARCH64_SEMANTIC_CONSTRAINT_PROGRAM_TOKEN_COUNT);
        for (u32 token = 0; token < constraint->feature_count; token += 1)
        {
            valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_constraint_feature_tags[constraint->feature_first + token], &ignored);
        }
        for (u32 token = 0; token < constraint->program_count; token += 1)
        {
            valid = valid && buster_a64_semantic_string_offset(buster_a64_semantic_constraint_program_tokens[constraint->program_first + token], &ignored);
        }
    }
    for (u32 preference_id = 0; preference_id < BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT; preference_id += 1)
    {
        BusterA64SemanticGeneratedAliasPreference const* preference = buster_a64_semantic_alias_preferences + preference_id;
        BusterA64SemanticString ignored;
        valid = valid && buster_a64_semantic_range(preference->condition_first, preference->condition_count, BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT) &&
                buster_a64_semantic_string_offset(preference->alias_file_offset, &ignored) && buster_a64_semantic_string_offset(preference->alias_id_offset, &ignored);
    }
    u32 alias_ids_seen = 0;
    for (u32 ordinal = 0; ordinal < BUSTER_AARCH64_SEMANTIC_ALIAS_COUNT; ordinal += 1)
    {
        u32 form_id = buster_a64_semantic_alias_form_ids[ordinal];
        valid = valid && form_id < BUSTER_AARCH64_SEMANTIC_FORM_COUNT && buster_a64_semantic_forms[form_id].kind == BUSTER_A64_SEMANTIC_FORM_ALIAS;
        for (u32 previous = 0; previous < ordinal; previous += 1)
        {
            valid = valid && buster_a64_semantic_alias_form_ids[previous] != form_id;
        }
        alias_ids_seen += form_id < BUSTER_AARCH64_SEMANTIC_FORM_COUNT && buster_a64_semantic_forms[form_id].kind == BUSTER_A64_SEMANTIC_FORM_ALIAS;
    }
    valid = valid && alias_ids_seen == BUSTER_AARCH64_SEMANTIC_ALIAS_COUNT;
    for (u32 field_id_index = 0; field_id_index < BUSTER_AARCH64_SEMANTIC_FIELD_COUNT; field_id_index += 1)
    {
        BusterA64SemanticGeneratedField field_value = {0};
        valid = valid && buster_a64_semantic_generated_field(field_id_index, &field_value);
        BusterA64SemanticGeneratedField const* field = &field_value;
        valid = valid && field->segment_count > 0 && field->width <= 32 && buster_a64_semantic_range(field->segment_first, field->segment_count, BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT);
        u32 source_mask = 0;
        for (u32 segment_index = 0; segment_index < field->segment_count; segment_index += 1)
        {
            BusterA64SemanticGeneratedSegment segment = {0};
            valid = valid && buster_a64_semantic_generated_segment(field->segment_first + segment_index, &segment);
            u32 mask = segment.width == 32 ? UINT32_MAX : ((UINT32_C(1) << segment.width) - 1);
            source_mask |= mask << segment.value_lsb;
        }
        valid = valid && source_mask == field->source_mask;
    }
    for (u32 index = 0; index < BUSTER_AARCH64_SEMANTIC_FORM_COUNT; index += 1)
    {
        BusterA64SemanticGeneratedForm const* form = buster_a64_semantic_forms + index;
        valid = valid && form->fixed_value == (form->fixed_value & form->fixed_mask) &&
                buster_a64_semantic_range(form->field_first, form->field_count, BUSTER_AARCH64_SEMANTIC_FIELD_COUNT) &&
                buster_a64_semantic_range(form->operand_first, form->operand_count, BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT) &&
                buster_a64_semantic_range(form->transform_first, form->transform_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT);
        if (form->owner < BUSTER_A64_SEMANTIC_OWNER_COUNT) owner_counts[form->owner] += 1; else valid = false;
        if (form->status == BUSTER_A64_SEMANTIC_STATUS_UNDEFINED) undefined_count += 1;
        for (u32 field_index = 0; field_index < form->field_count; field_index += 1)
        {
            BusterA64SemanticGeneratedField field_value = {0};
            valid = valid && buster_a64_semantic_generated_field(form->field_first + field_index, &field_value);
            BusterA64SemanticGeneratedField const* field = &field_value;
            BusterA64SemanticString ignored;
            valid = valid && buster_a64_semantic_string_offset(field->name_offset, &ignored);
        }
        for (u32 operand_index = 0; operand_index < form->operand_count; operand_index += 1)
        {
            BusterA64SemanticGeneratedOperand operand_value = {0};
            valid = valid && buster_a64_semantic_generated_operand(form->operand_first + operand_index, &operand_value);
            BusterA64SemanticGeneratedOperand const* operand = &operand_value;
            valid = valid && operand->form_id == index && operand->kind_mask != 0 && (operand->kind_mask & ~((UINT32_C(1) << 25) - 1)) == 0 &&
                    operand->classification_status == BUSTER_A64_SEMANTIC_CLASSIFICATION_PRESENTATION_ONLY && operand->reserved == 0 &&
                    buster_a64_semantic_range(operand->field_index_first, operand->field_index_count, BUSTER_AARCH64_SEMANTIC_OPERAND_FIELD_INDEX_COUNT) &&
                    buster_a64_semantic_range(operand->transform_first, operand->transform_count, BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT);
            for (u32 ref = 0; ref < operand->field_index_count; ref += 1)
            {
                valid = valid && buster_a64_semantic_operand_field_indices[operand->field_index_first + ref] < BUSTER_AARCH64_SEMANTIC_FIELD_COUNT;
            }
        }
    }
    valid = valid && owner_counts[BUSTER_A64_SEMANTIC_OWNER_ALIAS] == 172 &&
            owner_counts[BUSTER_A64_SEMANTIC_OWNER_FIXED32] == 32 && owner_counts[BUSTER_A64_SEMANTIC_OWNER_DIRECT_GPR] == 80 &&
            owner_counts[BUSTER_A64_SEMANTIC_OWNER_SCALAR_INTEGER] == 72 && owner_counts[BUSTER_A64_SEMANTIC_OWNER_DIRECT_SIMD] == 390 &&
            owner_counts[BUSTER_A64_SEMANTIC_OWNER_SYSTEM] == 18 && owner_counts[BUSTER_A64_SEMANTIC_OWNER_MEMORY] == 559 &&
            owner_counts[BUSTER_A64_SEMANTIC_OWNER_GENERAL_NONMEMORY] == 24 && owner_counts[BUSTER_A64_SEMANTIC_OWNER_COMPLEX_SIMD_FP] == 348 &&
            undefined_count == 1;
    return valid;
}

bool arm_a64_semantic_import_validate(void)
{
    return buster_a64_semantic_validate();
}

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/assembly/aarch64_semantics_internal.h>

BusterA64SemanticStorageTestResult buster_aarch64_semantics_test_storage(Arena* arena)
{
    BusterA64SemanticStorageTestResult result = {0};
#define BUSTER_A64_STORAGE_EXPECT(condition) do { result.checks += 1; result.failures += !(condition); } while (0)
    u8 guarded[132];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(guarded); index += 1)
    {
        guarded[index] = 0xa5;
    }
    BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_unpack_block(S8("/wA=").pointer, 4, guarded + 1, 130, 1));
    BUSTER_A64_STORAGE_EXPECT(guarded[0] == 0xa5 && guarded[131] == 0xa5);
    for (u32 index = 1; index <= 130; index += 1)
    {
        BUSTER_A64_STORAGE_EXPECT(guarded[index] == 0);
        guarded[index] = 0xa5;
    }
    BUSTER_A64_STORAGE_EXPECT(!buster_a64_semantic_unpack_block(S8("/wA=").pointer, 4, guarded + 1, 129, 1));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(guarded); index += 1)
    {
        BUSTER_A64_STORAGE_EXPECT(guarded[index] == 0xa5);
    }
    // Truncated run/literal, an ignored payload tail, and a non-group tail.
    BUSTER_A64_STORAGE_EXPECT(!buster_a64_semantic_unpack_block(S8("gA==").pointer, 4, guarded, 3, 1));
    BUSTER_A64_STORAGE_EXPECT(!buster_a64_semantic_unpack_block(S8("AgE=").pointer, 4, guarded, 3, 1));
    BUSTER_A64_STORAGE_EXPECT(!buster_a64_semantic_unpack_block(S8("AAMAAA==").pointer, 8, guarded, 1, 1));
    BUSTER_A64_STORAGE_EXPECT(!buster_a64_semantic_unpack_block(S8("AAA").pointer, 3, guarded, 1, 1));
    BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_unpack_block(0, 0, 0, 0, 1));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(guarded); index += 1)
    {
        guarded[index] = 0xa5;
    }
    String8 literal = S8("fwABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/");
    BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_unpack_block(literal.pointer, (u32)literal.length, guarded + 1, 128, 1));
    for (u32 index = 0; index < 128; index += 1)
    {
        BUSTER_A64_STORAGE_EXPECT(guarded[index + 1] == index);
    }
    BUSTER_A64_STORAGE_EXPECT(guarded[0] == 0xa5 && guarded[129] == 0xa5);
    // A run crosses the column boundary and a following literal continues it.
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(guarded); index += 1)
    {
        guarded[index] = 0xa5;
    }
    BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_unpack_block(S8("gVoBYmM=").pointer, 8, guarded + 1, 6, 2));
    u8 const crossed[] = {0x5a, 0x5a, 0x5a, 0x62, 0x5a, 0x63};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(crossed); index += 1)
    {
        BUSTER_A64_STORAGE_EXPECT(guarded[index + 1] == crossed[index]);
        guarded[index + 1] = 0xa5;
    }
    BUSTER_A64_STORAGE_EXPECT(guarded[0] == 0xa5 && guarded[7] == 0xa5);
    BUSTER_A64_STORAGE_EXPECT(!buster_a64_semantic_unpack_block(S8("gVoBYmM=").pointer, 8, guarded + 1, 6, 0));
    BUSTER_A64_STORAGE_EXPECT(!buster_a64_semantic_unpack_block(S8("gVoBYmM=").pointer, 8, guarded + 1, 5, 2));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(guarded); index += 1)
    {
        BUSTER_A64_STORAGE_EXPECT(guarded[index] == 0xa5);
    }
    // Two byte columns, one complete block followed by three final records.
    char8 const fixture_data[] = "PwACBAYICgwOEBIUFhgaHB4gIiQmKCosLjAyNDY4Ojw+AQMFBwkLDQ8RExUXGRsdHyEjJScpKy0vMTM1Nzk7PT8=BUBCREFDRQ==";
    u32 const fixture_blocks[] = {0, 88, 100};
    BusterA64SemanticGeneratedBlob fixture = {fixture_data, fixture_blocks, 2, 0};
    u8 fixture_bytes[70];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fixture_bytes); index += 1)
    {
        fixture_bytes[index] = 0xa5;
    }
    BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_fill(&fixture, fixture_bytes, sizeof(fixture_bytes)));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fixture_bytes); index += 1)
    {
        BUSTER_A64_STORAGE_EXPECT(fixture_bytes[index] == index);
        BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_decode(&fixture, sizeof(fixture_bytes), index) == index);
        BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_cached_byte(&fixture, sizeof(fixture_bytes), index) == index);
    }
    BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_decode(&fixture, sizeof(fixture_bytes), 70) == 0);
    BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_decode(&fixture, sizeof(fixture_bytes), UINT32_MAX) == 0);
    BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_cached_byte(&fixture, sizeof(fixture_bytes), UINT32_MAX) == 0);
    BUSTER_A64_STORAGE_EXPECT(!buster_a64_semantic_blob_fill_block(&fixture, sizeof(fixture_bytes), UINT32_MAX, fixture_bytes));
    struct
    {
        BusterA64SemanticGeneratedBlob const* blob;
        u8 const* bytes;
        u32 size;
        u64 expected;
    } tables[] = {
        {&buster_a64_semantic_segment_blob, buster_a64_semantic_segment_bytes, BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE, UINT64_C(0x872a0037d89d2071)},
        {&buster_a64_semantic_field_blob, buster_a64_semantic_field_bytes, BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE, UINT64_C(0x5c38755e61eebdc0)},
        {&buster_a64_semantic_operand_blob, buster_a64_semantic_operand_bytes, BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE, UINT64_C(0xf3e46611ec58564a)},
        {&buster_a64_semantic_value_atom_blob, buster_a64_semantic_value_atom_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE, UINT64_C(0x566fbe062f47516d)},
        {&buster_a64_semantic_value_entry_blob, buster_a64_semantic_value_entry_bytes, BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE, UINT64_C(0xf425646c9fb33ea5)},
    };
    bool ready_before = buster_a64_semantic_bytes_ready;
    TemporalArena temporary = scratch_begin(&arena, 1);
    for (u32 table = 0; table < BUSTER_ARRAY_LENGTH(tables); table += 1)
    {
        // These hashes were computed from the preceding uncompressed wire
        // payloads, not from this decoder or the new packed representation.
        TemporalArena buffer = arena_begin_temporal(temporary.arena);
        u8* restored = arena_allocate(temporary.arena, u8, tables[table].size);
        BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_fill(tables[table].blob, restored, tables[table].size));
        u64 hash = UINT64_C(14695981039346656037);
        bool cache_matches = true;
        for (u32 index = 0; index < tables[table].size; index += 1)
        {
            hash = (hash ^ restored[index]) * UINT64_C(1099511628211);
            cache_matches &= !ready_before || restored[index] == tables[table].bytes[index];
        }
        BUSTER_A64_STORAGE_EXPECT(hash == tables[table].expected);
        BUSTER_A64_STORAGE_EXPECT(cache_matches);
        u32 width = tables[table].blob->record_bytes;
        u32 block_bytes = width * BUSTER_AARCH64_SEMANTIC_PACKED_BLOCK_RECORDS;
        for (u32 first = 0; first < tables[table].size; first += block_bytes)
        {
            u32 remaining = tables[table].size - first;
            u32 size = BUSTER_MIN(block_bytes, remaining);
            for (u32 column = 0; column < width; column += 1)
            {
                u32 last = first + size - width + column;
                BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_decode(tables[table].blob, tables[table].size, first + column) == restored[first + column]);
                BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_decode(tables[table].blob, tables[table].size, last) == restored[last]);
                BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_cached_byte(tables[table].blob, tables[table].size, first + column) == restored[first + column]);
                BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_cached_byte(tables[table].blob, tables[table].size, last) == restored[last]);
                BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_byte(tables[table].blob, tables[table].bytes, tables[table].size, last) == restored[last]);
            }
        }
        // Numeric spans include a block boundary and a short final range;
        // missing bytes retain the preceding zero-fill behavior.
        u32 offsets[] = {0, block_bytes - 1, tables[table].size - 1, tables[table].size};
        for (u32 sample = 0; sample < BUSTER_ARRAY_LENGTH(offsets); sample += 1)
        {
            u32 offset = offsets[sample];
            u64 expected = 0;
            for (u32 byte = 0; byte < 8; byte += 1)
            {
                if (offset + byte < tables[table].size)
                {
                    expected |= (u64)restored[offset + byte] << (8 * byte);
                }
            }
            BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_u16(tables[table].blob, tables[table].bytes, tables[table].size, offset) == (u16)expected);
            BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_u32(tables[table].blob, tables[table].bytes, tables[table].size, offset) == (u32)expected);
            BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_blob_u64(tables[table].blob, tables[table].bytes, tables[table].size, offset) == expected);
        }
        // Reuse previously published first/final blocks, interleaving blobs.
        for (u32 repeat = 0; repeat < 2; repeat += 1)
        {
            for (u32 other = 0; other < BUSTER_ARRAY_LENGTH(tables); other += 1)
            {
                u32 index = repeat ? tables[other].size - 1 : 0;
                BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_cached_byte(tables[other].blob, tables[other].size, index) ==
                    buster_a64_semantic_blob_decode(tables[other].blob, tables[other].size, index));
            }
        }
        scratch_end(buffer);
    }
    BUSTER_A64_STORAGE_EXPECT(buster_a64_semantic_bytes_ready == ready_before);
    scratch_end(temporary);
#undef BUSTER_A64_STORAGE_EXPECT
    return result;
}
#endif
