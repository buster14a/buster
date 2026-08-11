#include <buster/lib/compiler/assembly/aarch64_syntax.h>

#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif
#include <buster/lib/compiler/assembly/generated/aarch64-syntax.generated.h>
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic pop
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

typedef struct BusterAarch64SyntaxMatchState BusterAarch64SyntaxMatchState;
struct BusterAarch64SyntaxMatchState
{
    String8 input;
    u64 cursor;
    u32 anchor_index;
    BusterAarch64SyntaxCallbacks callbacks;
};

typedef struct BusterAarch64SyntaxPrintState BusterAarch64SyntaxPrintState;
struct BusterAarch64SyntaxPrintState
{
    BusterAarch64SyntaxOutput* output;
    u32 anchor_index;
    bool concrete;
    BusterAarch64SyntaxCallbacks callbacks;
};

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_callbacks_transactional(BusterAarch64SyntaxCallbacks callbacks)
{
    return (callbacks.checkpoint == 0) == (callbacks.restore == 0);
}

BUSTER_GLOBAL_LOCAL u64 buster_aarch64_syntax_callback_checkpoint(BusterAarch64SyntaxCallbacks callbacks)
{
    return callbacks.checkpoint ? callbacks.checkpoint(callbacks.user) : 0;
}

BUSTER_GLOBAL_LOCAL void buster_aarch64_syntax_callback_restore(BusterAarch64SyntaxCallbacks callbacks, u64 token)
{
    if (callbacks.restore) callbacks.restore(callbacks.user, token);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_pool_range_valid(u32 offset, u32 length)
{
    return offset <= BUSTER_AARCH64_SYNTAX_GENERATED_STRING_POOL_SIZE &&
           length <= BUSTER_AARCH64_SYNTAX_GENERATED_STRING_POOL_SIZE - offset;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_bytes_equal(String8 left, String8 right)
{
    if (left.length != right.length || (!left.pointer && left.length) || (!right.pointer && right.length)) return false;
    return left.length == 0 || memcmp(left.pointer, right.pointer, (size_t)left.length) == 0;
}

BUSTER_GLOBAL_LOCAL String8 buster_aarch64_syntax_pool_string(u32 offset, u32 length)
{
    if (!buster_aarch64_syntax_pool_range_valid(offset, length)) return (String8){0};
    return (String8){.pointer = (char8*)(buster_aarch64_syntax_generated_string_pool + offset), .length = length};
}

BUSTER_GLOBAL_LOCAL u8 buster_aarch64_syntax_base64_value(char8 character)
{
    if (character >= 'A' && character <= 'Z') return (u8)(character - 'A');
    if (character >= 'a' && character <= 'z') return (u8)(character - 'a' + 26);
    if (character >= '0' && character <= '9') return (u8)(character - '0' + 52);
    if (character == '+') return 62;
    if (character == '/') return 63;
    return 0xff;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_packed_byte(char8 const* blob, u32 blob_length, u32 byte_index, u8* result)
{
    if (!blob || !result || byte_index > UINT32_MAX / 4 * 3) return false;
    u32 group = byte_index / 3;
    u32 within = byte_index % 3;
    u32 encoded = group * 4;
    if (encoded > blob_length || blob_length - encoded < 4) return false;
    u8 a = buster_aarch64_syntax_base64_value(blob[encoded + 0]);
    u8 b = buster_aarch64_syntax_base64_value(blob[encoded + 1]);
    u8 c = blob[encoded + 2] == '=' ? 0 : buster_aarch64_syntax_base64_value(blob[encoded + 2]);
    u8 d = blob[encoded + 3] == '=' ? 0 : buster_aarch64_syntax_base64_value(blob[encoded + 3]);
    if (a == 0xff || b == 0xff || (blob[encoded + 2] != '=' && c == 0xff) ||
        (blob[encoded + 3] != '=' && d == 0xff)) return false;
    u32 bits = ((u32)a << 18) | ((u32)b << 12) | ((u32)c << 6) | (u32)d;
    *result = (u8)(bits >> (16 - 8 * within));
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_packed_bytes(char8 const* blob, u32 blob_length, u32 byte_offset,
                                                              u8* result, u32 result_length)
{
    if (!result || result_length > UINT32_MAX - byte_offset) return false;
    for (u32 offset = 0; offset < result_length; offset += 1)
    {
        if (!buster_aarch64_syntax_packed_byte(blob, blob_length, byte_offset + offset, result + offset)) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u32 buster_aarch64_syntax_read_u32(u8 const* bytes)
{
    return (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

BUSTER_GLOBAL_LOCAL u16 buster_aarch64_syntax_read_u16(u8 const* bytes)
{
    return (u16)((u16)bytes[0] | ((u16)bytes[1] << 8));
}

BUSTER_GLOBAL_LOCAL u64 buster_aarch64_syntax_read_u64(u8 const* bytes)
{
    u64 result = 0;
    for (u32 offset = 0; offset < 8; offset += 1)
    {
        result |= (u64)bytes[offset] << (8 * offset);
    }
    return result;
}

/* Decode the compact generated projection once into bounded, pointer-free
 * runtime records.  Keeping the checked-in source as blobs avoids burdening
 * the self-host parser, while normal metadata access remains O(1). */
BUSTER_GLOBAL_LOCAL BusterAarch64SyntaxGeneratedNode
    buster_aarch64_syntax_decoded_nodes[BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT];
BUSTER_GLOBAL_LOCAL BusterAarch64SyntaxGeneratedRow
    buster_aarch64_syntax_decoded_rows[BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_aarch64_syntax_decoded_child_indices[BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_COUNT];
BUSTER_GLOBAL_LOCAL BusterAarch64SyntaxGeneratedMnemonicRange
    buster_aarch64_syntax_decoded_ranges[BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_aarch64_syntax_decoded_candidates[BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_aarch64_syntax_decoded_tables_state;

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_decode_tables(void)
{
    if (buster_aarch64_syntax_decoded_tables_state == 1) return true;
    for (u32 index = 0; index < BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT; index += 1)
    {
        u8 bytes[20] = {0};
        if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_nodes_blob,
                                                (u32)(sizeof(buster_aarch64_syntax_generated_nodes_blob) - 1),
                                                index * BUSTER_AARCH64_SYNTAX_GENERATED_NODE_RECORD_BYTES, bytes, sizeof(bytes))) return false;
        buster_aarch64_syntax_decoded_nodes[index] = (BusterAarch64SyntaxGeneratedNode){
            .child_first = buster_aarch64_syntax_read_u32(bytes + 0),
            .child_count = buster_aarch64_syntax_read_u32(bytes + 4),
            .text_offset = buster_aarch64_syntax_read_u32(bytes + 8),
            .text_length = buster_aarch64_syntax_read_u32(bytes + 12),
            .kind_flags = buster_aarch64_syntax_read_u16(bytes + 16),
            .numeric_count = buster_aarch64_syntax_read_u16(bytes + 18),
        };
    }
    for (u32 index = 0; index < BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_COUNT; index += 1)
    {
        u8 bytes[4] = {0};
        if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_child_indices_blob,
                                                (u32)(sizeof(buster_aarch64_syntax_generated_child_indices_blob) - 1),
                                                index * BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_RECORD_BYTES, bytes, sizeof(bytes))) return false;
        buster_aarch64_syntax_decoded_child_indices[index] = buster_aarch64_syntax_read_u32(bytes);
    }
    for (u32 index = 0; index < BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT; index += 1)
    {
        u8 bytes[56] = {0};
        if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_rows_blob,
                                                (u32)(sizeof(buster_aarch64_syntax_generated_rows_blob) - 1),
                                                index * BUSTER_AARCH64_SYNTAX_GENERATED_ROW_RECORD_BYTES, bytes, sizeof(bytes))) return false;
        buster_aarch64_syntax_decoded_rows[index] = (BusterAarch64SyntaxGeneratedRow){
            .node_first = buster_aarch64_syntax_read_u32(bytes + 0),
            .node_count = buster_aarch64_syntax_read_u32(bytes + 4),
            .id_offset = buster_aarch64_syntax_read_u32(bytes + 8),
            .id_length = buster_aarch64_syntax_read_u32(bytes + 12),
            .assembly_offset = buster_aarch64_syntax_read_u32(bytes + 16),
            .assembly_length = buster_aarch64_syntax_read_u32(bytes + 20),
            .mnemonic_offset = buster_aarch64_syntax_read_u32(bytes + 24),
            .mnemonic_length = buster_aarch64_syntax_read_u32(bytes + 28),
            .anchor_count = buster_aarch64_syntax_read_u32(bytes + 32),
            .anchor_min = buster_aarch64_syntax_read_u32(bytes + 36),
            .anchor_max = buster_aarch64_syntax_read_u32(bytes + 40),
            .row_kind = buster_aarch64_syntax_read_u32(bytes + 44),
            .source_hash = buster_aarch64_syntax_read_u64(bytes + 48),
        };
    }
    for (u32 index = 0; index < BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_COUNT; index += 1)
    {
        u8 bytes[16] = {0};
        if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_mnemonic_ranges_blob,
                                                (u32)(sizeof(buster_aarch64_syntax_generated_mnemonic_ranges_blob) - 1),
                                                index * BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_RECORD_BYTES, bytes, sizeof(bytes))) return false;
        buster_aarch64_syntax_decoded_ranges[index] = (BusterAarch64SyntaxGeneratedMnemonicRange){
            .key_offset = buster_aarch64_syntax_read_u32(bytes + 0),
            .key_length = buster_aarch64_syntax_read_u32(bytes + 4),
            .candidate_first = buster_aarch64_syntax_read_u32(bytes + 8),
            .candidate_count = buster_aarch64_syntax_read_u32(bytes + 12),
        };
    }
    for (u32 index = 0; index < BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT; index += 1)
    {
        u8 bytes[4] = {0};
        if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_mnemonic_candidates_blob,
                                                (u32)(sizeof(buster_aarch64_syntax_generated_mnemonic_candidates_blob) - 1),
                                                index * BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_RECORD_BYTES, bytes, sizeof(bytes))) return false;
        buster_aarch64_syntax_decoded_candidates[index] = buster_aarch64_syntax_read_u32(bytes);
    }
    buster_aarch64_syntax_decoded_tables_state = 1;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_node_at(u32 index, BusterAarch64SyntaxGeneratedNode* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT ||
        index > (UINT32_MAX - 20u) / BUSTER_AARCH64_SYNTAX_GENERATED_NODE_RECORD_BYTES) return false;
    if (!buster_aarch64_syntax_decode_tables()) return false;
    *result = buster_aarch64_syntax_decoded_nodes[index];
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_row_at(u32 index, BusterAarch64SyntaxGeneratedRow* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT ||
        index > (UINT32_MAX - BUSTER_AARCH64_SYNTAX_GENERATED_ROW_RECORD_BYTES) / BUSTER_AARCH64_SYNTAX_GENERATED_ROW_RECORD_BYTES) return false;
    if (!buster_aarch64_syntax_decode_tables()) return false;
    *result = buster_aarch64_syntax_decoded_rows[index];
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_child_at(u32 index, u32* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_COUNT ||
        index > (UINT32_MAX - BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_RECORD_BYTES) /
                    BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_RECORD_BYTES) return false;
    if (!buster_aarch64_syntax_decode_tables()) return false;
    *result = buster_aarch64_syntax_decoded_child_indices[index];
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_range_at(u32 index, BusterAarch64SyntaxGeneratedMnemonicRange* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_COUNT ||
        index > (UINT32_MAX - BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_RECORD_BYTES) /
                    BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_RECORD_BYTES) return false;
    if (!buster_aarch64_syntax_decode_tables()) return false;
    *result = buster_aarch64_syntax_decoded_ranges[index];
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_candidate_at(u32 index, u32* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT ||
        index > (UINT32_MAX - BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_RECORD_BYTES) /
                    BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_RECORD_BYTES) return false;
    if (!buster_aarch64_syntax_decode_tables()) return false;
    *result = buster_aarch64_syntax_decoded_candidates[index];
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_node_valid(u32 index)
{
    if (index >= BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT) return false;
    BusterAarch64SyntaxGeneratedNode node = {0};
    if (!buster_aarch64_syntax_generated_node_at(index, &node)) return false;
    return buster_aarch64_syntax_pool_range_valid(node.text_offset, node.text_length) &&
           node.child_first <= BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_COUNT &&
           node.child_count <= BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_COUNT - node.child_first;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_row_valid(u32 index)
{
    if (index >= BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT) return false;
    BusterAarch64SyntaxGeneratedRow row = {0};
    if (!buster_aarch64_syntax_generated_row_at(index, &row)) return false;
    return row.node_first <= BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT &&
           row.node_count <= BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT - row.node_first &&
           buster_aarch64_syntax_pool_range_valid(row.id_offset, row.id_length) &&
           buster_aarch64_syntax_pool_range_valid(row.assembly_offset, row.assembly_length) &&
           buster_aarch64_syntax_pool_range_valid(row.mnemonic_offset, row.mnemonic_length) &&
           row.anchor_min <= row.anchor_max && row.anchor_max <= row.anchor_count;
}

BUSTER_F_DECL u32 buster_aarch64_syntax_schema_version(void)
{
    return BUSTER_AARCH64_SYNTAX_GENERATED_SCHEMA_VERSION;
}

BUSTER_F_DECL bool buster_aarch64_syntax_string(u32 offset, u32 length, String8* result)
{
    if (!result || !buster_aarch64_syntax_pool_range_valid(offset, length)) return false;
    *result = buster_aarch64_syntax_pool_string(offset, length);
    return true;
}

BUSTER_F_DECL bool buster_aarch64_syntax_node(u32 index, BusterAarch64SyntaxNode* result)
{
    if (!result || !buster_aarch64_syntax_generated_node_valid(index)) return false;
    BusterAarch64SyntaxGeneratedNode node = {0};
    if (!buster_aarch64_syntax_generated_node_at(index, &node)) return false;
    u16 kind_flags = node.kind_flags;
    u8 kind = (u8)(kind_flags & 0xffu);
    u8 flags = (u8)(kind_flags >> 8);
    if (kind >= BUSTER_AARCH64_SYNTAX_NODE_KIND_COUNT) return false;
    *result = (BusterAarch64SyntaxNode){
        .kind = (BusterAarch64SyntaxNodeKind)kind,
        .flags = flags,
        .reserved = 0,
        .index = index,
        .child_first = node.child_first,
        .child_count = node.child_count,
        .numeric_count = node.numeric_count,
        .text = buster_aarch64_syntax_pool_string(node.text_offset, node.text_length),
    };
    return true;
}

BUSTER_F_DECL bool buster_aarch64_syntax_node_range_valid(u32 first, u32 count)
{
    return first <= BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT && count <= BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT - first;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_child_range_valid(u32 first, u32 count)
{
    return first <= BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_COUNT &&
           count <= BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_COUNT - first;
}

BUSTER_F_DECL bool buster_aarch64_syntax_node_child(BusterAarch64SyntaxNode node, u32 child_index, u32* result)
{
    if (!result || child_index >= node.child_count || !buster_aarch64_syntax_child_range_valid(node.child_first, node.child_count)) return false;
    u32 index = 0;
    if (!buster_aarch64_syntax_generated_child_at(node.child_first + child_index, &index)) return false;
    if (!buster_aarch64_syntax_generated_node_valid(index)) return false;
    *result = index;
    return true;
}

BUSTER_F_DECL bool buster_aarch64_syntax_row(u32 index, BusterAarch64SyntaxRow* result)
{
    if (!result || !buster_aarch64_syntax_generated_row_valid(index)) return false;
    BusterAarch64SyntaxGeneratedRow row = {0};
    if (!buster_aarch64_syntax_generated_row_at(index, &row)) return false;
    if (row.row_kind >= BUSTER_AARCH64_SYNTAX_ROW_KIND_COUNT) return false;
    *result = (BusterAarch64SyntaxRow){
        .index = index,
        .node_first = row.node_first,
        .node_count = row.node_count,
        .anchor_count = row.anchor_count,
        .anchor_min = row.anchor_min,
        .anchor_max = row.anchor_max,
        .kind = (BusterAarch64SyntaxRowKind)row.row_kind,
        .source_hash = row.source_hash,
        .id = buster_aarch64_syntax_pool_string(row.id_offset, row.id_length),
        .assembly = buster_aarch64_syntax_pool_string(row.assembly_offset, row.assembly_length),
        .mnemonic = buster_aarch64_syntax_pool_string(row.mnemonic_offset, row.mnemonic_length),
    };
    return true;
}

BUSTER_F_DECL bool buster_aarch64_syntax_mnemonic_range_at(u32 index, BusterAarch64SyntaxMnemonicRange* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_COUNT) return false;
    BusterAarch64SyntaxGeneratedMnemonicRange range = {0};
    if (!buster_aarch64_syntax_generated_range_at(index, &range)) return false;
    if (!buster_aarch64_syntax_pool_range_valid(range.key_offset, range.key_length) ||
        range.candidate_first > BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT ||
        range.candidate_count > BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT - range.candidate_first)
        return false;
    *result = (BusterAarch64SyntaxMnemonicRange){
        .key = buster_aarch64_syntax_pool_string(range.key_offset, range.key_length),
        .candidate_first = range.candidate_first,
        .candidate_count = range.candidate_count,
    };
    return true;
}

BUSTER_F_DECL bool buster_aarch64_syntax_mnemonic_lookup(String8 mnemonic, BusterAarch64SyntaxMnemonicRange* result)
{
    if (!result || !mnemonic.length || !mnemonic.pointer) return false;
    u32 low = 0;
    u32 high = BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_COUNT;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        BusterAarch64SyntaxMnemonicRange range = {0};
        if (!buster_aarch64_syntax_mnemonic_range_at(middle, &range)) return false;
        // Generated keys are uppercase ASCII and sorted lexicographically.
        u64 common = BUSTER_MIN(mnemonic.length, range.key.length);
        int compare = 0;
        for (u64 index = 0; index < common; index += 1)
        {
            u8 left = (u8)mnemonic.pointer[index];
            u8 right = (u8)range.key.pointer[index];
            if (left >= 'a' && left <= 'z') left = (u8)(left - 'a' + 'A');
            if (left != right)
            {
                compare = left < right ? -1 : 1;
                break;
            }
        }
        if (!compare && mnemonic.length != range.key.length) compare = mnemonic.length < range.key.length ? -1 : 1;
        if (!compare)
        {
            *result = range;
            return true;
        }
        if (compare < 0) high = middle;
        else low = middle + 1;
    }
    return false;
}

BUSTER_F_DECL bool buster_aarch64_syntax_mnemonic_candidate(BusterAarch64SyntaxMnemonicRange range, u32 index, u32* row_index)
{
    if (!row_index || range.candidate_first > BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT ||
        range.candidate_count > BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT - range.candidate_first ||
        index >= range.candidate_count)
        return false;
    u32 candidate = 0;
    if (!buster_aarch64_syntax_generated_candidate_at(range.candidate_first + index, &candidate)) return false;
    if (candidate >= BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT) return false;
    *row_index = candidate;
    return true;
}

BUSTER_F_DECL BusterAarch64SyntaxCounts buster_aarch64_syntax_counts(void)
{
    BusterAarch64SyntaxCounts result = {
        .row_count = BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT,
        .string_pool_bytes = BUSTER_AARCH64_SYNTAX_GENERATED_STRING_POOL_SIZE,
        .mnemonic_range_count = BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_COUNT,
        .mnemonic_candidate_count = BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT,
        .node_count = BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT,
    };
    for (u32 index = 0; index < result.row_count; index += 1)
    {
        BusterAarch64SyntaxGeneratedRow row = {0};
        if (!buster_aarch64_syntax_generated_row_at(index, &row)) continue;
        if (row.row_kind == BUSTER_AARCH64_SYNTAX_ROW_CANONICAL) result.canonical_row_count += 1;
        else if (row.row_kind == BUSTER_AARCH64_SYNTAX_ROW_ALIAS) result.alias_row_count += 1;
    }
    for (u32 index = 0; index < result.node_count; index += 1)
    {
        BusterAarch64SyntaxGeneratedNode generated = {0};
        if (!buster_aarch64_syntax_generated_node_at(index, &generated)) continue;
        u8 kind = (u8)(generated.kind_flags & 0xffu);
        u8 flags = (u8)(generated.kind_flags >> 8);
        if (kind == BUSTER_AARCH64_SYNTAX_OPTIONAL) result.optional_node_count += 1;
        else if (kind == BUSTER_AARCH64_SYNTAX_ALT) result.alt_node_count += 1;
        else if (kind == BUSTER_AARCH64_SYNTAX_MEM)
        {
            result.mem_node_count += 1;
            result.mem_writeback_count += (flags & BUSTER_AARCH64_SYNTAX_MEM_WRITEBACK) != 0;
        }
        else if (kind == BUSTER_AARCH64_SYNTAX_LIST) result.list_node_count += 1;
        else if (kind == BUSTER_AARCH64_SYNTAX_LANE) result.lane_node_count += 1;
        else if (kind == BUSTER_AARCH64_SYNTAX_ANCHOR)
        {
            result.anchor_occurrence_count += 1;
            result.anchor_alternative_count += (flags & BUSTER_AARCH64_SYNTAX_ANCHOR_ALTERNATIVE) != 0;
            result.range_anchor_count += (flags & BUSTER_AARCH64_SYNTAX_ANCHOR_RANGE) != 0;
        }
        else if (kind == BUSTER_AARCH64_SYNTAX_MNEMONIC)
        {
            result.mnemonic_optional_suffix_count += (flags & BUSTER_AARCH64_SYNTAX_MNEMONIC_OPTIONAL_SUFFIX) != 0;
            result.mnemonic_condition_count += (flags & BUSTER_AARCH64_SYNTAX_MNEMONIC_CONDITION) != 0;
        }
        if (kind == BUSTER_AARCH64_SYNTAX_LIT) result.fixed_numeric_literal_count += generated.numeric_count;
    }
    return result;
}

BUSTER_F_DECL BusterAarch64SyntaxStats buster_aarch64_syntax_stats(void)
{
    return (BusterAarch64SyntaxStats){
        .generic_shape_count = 181,
        .exact_shape_count = 1635,
        .max_total_ast_nodes = 29,
        .max_non_lit_non_seq_nodes = 14,
        .max_optional_depth = 2,
        .max_delimiter_nesting = 3,
        .max_top_level_comma_groups = 5,
        .max_anchor_operands = 10,
        .input_digest_hi = UINT64_C(0x7dccd8605bfe3f3f),
        .input_digest_lo = UINT64_C(0x738e8e070468625a),
    };
}

BUSTER_F_DECL String8 buster_aarch64_syntax_input_digest(void)
{
    return S8("7dccd8605bfe3f3f738e8e070468625ae364c97c7901b119631b2f54396243ac");
}
BUSTER_F_DECL String8 buster_aarch64_syntax_generic_shape_digest(void)
{
    return S8("c81a31eaf080057e934b03f9bbe265fb267b48e6684bcd1767809fd4be05c3b2");
}
BUSTER_F_DECL String8 buster_aarch64_syntax_generic_row_digest(void)
{
    return S8("aca86f91a674243d3782f50c725c6e52a9e768680200d9c74f0c8d4130f1e874");
}
BUSTER_F_DECL String8 buster_aarch64_syntax_exact_shape_digest(void)
{
    return S8("726948d2c5db9d57b84e47432253aed229904296dcab5dc3e4ea3a66ceafe8f8");
}
BUSTER_F_DECL String8 buster_aarch64_syntax_exact_row_digest(void)
{
    return S8("d119218081214274587a380f5a5f5336eaf06de979cd66c77b14dcc133461250");
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_input_take(String8 input, u64* cursor, String8 literal)
{
    if (!cursor || *cursor > input.length || literal.length > input.length - *cursor) return false;
    if (!buster_aarch64_syntax_bytes_equal((String8){.pointer = input.pointer + *cursor, .length = literal.length}, literal)) return false;
    *cursor += literal.length;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_input_take_mnemonic(String8 input, u64* cursor, String8 literal)
{
    if (!cursor || *cursor > input.length || literal.length > input.length - *cursor) return false;
    for (u64 offset = 0; offset < literal.length; offset += 1)
    {
        u8 expected = (u8)literal.pointer[offset];
        u8 actual = (u8)input.pointer[*cursor + offset];
        if (expected >= 'a' && expected <= 'z') expected = (u8)(expected - 'a' + 'A');
        if (actual >= 'a' && actual <= 'z') actual = (u8)(actual - 'a' + 'A');
        if (expected != actual) return false;
    }
    *cursor += literal.length;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_node(u32 index, BusterAarch64SyntaxMatchState* state, u32 optional_depth);

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_children(u32 first, u32 count, BusterAarch64SyntaxMatchState* state,
                                                               u32 optional_depth)
{
    if (!buster_aarch64_syntax_child_range_valid(first, count)) return false;
    for (u32 child_offset = 0; child_offset < count; child_offset += 1)
    {
        u32 child = 0;
        if (!buster_aarch64_syntax_generated_child_at(first + child_offset, &child)) return false;
        if (!buster_aarch64_syntax_match_node(child, state, optional_depth)) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_anchor(u32 index, BusterAarch64SyntaxMatchState* state)
{
    BusterAarch64SyntaxNode node = {0};
    if (!buster_aarch64_syntax_node(index, &node) || node.kind != BUSTER_AARCH64_SYNTAX_ANCHOR ||
        !state->callbacks.match_anchor || state->anchor_index == UINT32_MAX)
        return false;
    BusterAarch64SyntaxAnchor anchor = {
        .node_index = index,
        .occurrence = state->anchor_index,
        .flags = node.flags,
        .spelling = node.text,
    };
    u64 cursor = state->cursor;
    u64 token = buster_aarch64_syntax_callback_checkpoint(state->callbacks);
    if (!state->callbacks.match_anchor(state->callbacks.user, anchor, state->input, &cursor) || cursor > state->input.length)
    {
        buster_aarch64_syntax_callback_restore(state->callbacks, token);
        return false;
    }
    state->cursor = cursor;
    state->anchor_index += 1;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_alt(u32 index, BusterAarch64SyntaxNode node,
                                                          BusterAarch64SyntaxMatchState* state, u32 optional_depth)
{
    BUSTER_UNUSED(index);
    bool delimited = !(node.flags & BUSTER_AARCH64_SYNTAX_ALT_IMPLICIT_DELIMITER);
    bool implicit_canonical = false;
    if (!delimited)
    {
        u64 look = state->cursor;
        for (; look < state->input.length && state->input.pointer[look] != '}'; look += 1)
        {
            if (state->input.pointer[look] == '|')
            {
                implicit_canonical = true;
                break;
            }
        }
    }
    if ((delimited && state->cursor < state->input.length && state->input.pointer[state->cursor] == '(') || implicit_canonical)
    {
        u64 saved_cursor = state->cursor;
        u32 saved_anchor_index = state->anchor_index;
        u64 saved_token = buster_aarch64_syntax_callback_checkpoint(state->callbacks);
        if (delimited)
        {
            u64 cursor = state->cursor;
            if (!buster_aarch64_syntax_input_take(state->input, &cursor, S8("(")))
            {
                buster_aarch64_syntax_callback_restore(state->callbacks, saved_token);
                return false;
            }
            state->cursor = cursor;
        }
        if (!buster_aarch64_syntax_match_children(node.child_first, node.child_count, state, optional_depth) ||
            (delimited && !buster_aarch64_syntax_input_take(state->input, &state->cursor, S8(")"))))
        {
            state->cursor = saved_cursor;
            state->anchor_index = saved_anchor_index;
            buster_aarch64_syntax_callback_restore(state->callbacks, saved_token);
            return false;
        }
        return true;
    }

    /* Branch separators are literal `|` nodes at this level. */
    u32 branch_first = node.child_first;
    u32 remaining = node.child_count;
    for (;;)
    {
        u32 branch_count = 0;
        while (branch_count < remaining)
        {
            BusterAarch64SyntaxNode separator = {0};
            bool is_separator = false;
            if (branch_count < remaining)
            {
                u32 separator_index = 0;
                is_separator = buster_aarch64_syntax_generated_child_at(branch_first + branch_count, &separator_index) &&
                               buster_aarch64_syntax_node(separator_index, &separator) && separator.kind == BUSTER_AARCH64_SYNTAX_LIT &&
                               buster_aarch64_syntax_bytes_equal(separator.text, S8("|"));
            }
            if (is_separator)
                break;
            branch_count += 1;
        }
        u64 cursor = state->cursor;
        u32 anchor_index = state->anchor_index;
        u64 token = buster_aarch64_syntax_callback_checkpoint(state->callbacks);
        if (buster_aarch64_syntax_match_children(branch_first, branch_count, state, optional_depth)) return true;
        state->cursor = cursor;
        state->anchor_index = anchor_index;
        buster_aarch64_syntax_callback_restore(state->callbacks, token);
        if (branch_count == remaining) break;
        branch_first += branch_count + 1;
        remaining -= branch_count + 1;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_node(u32 index, BusterAarch64SyntaxMatchState* state, u32 optional_depth)
{
    BusterAarch64SyntaxNode node = {0};
    if (!state || !buster_aarch64_syntax_node(index, &node)) return false;
    if (node.kind == BUSTER_AARCH64_SYNTAX_SEQ || node.kind == BUSTER_AARCH64_SYNTAX_MEM ||
        node.kind == BUSTER_AARCH64_SYNTAX_LIST || node.kind == BUSTER_AARCH64_SYNTAX_LANE ||
        node.kind == BUSTER_AARCH64_SYNTAX_MNEMONIC)
    {
        if (node.kind == BUSTER_AARCH64_SYNTAX_MNEMONIC)
            return buster_aarch64_syntax_input_take_mnemonic(state->input, &state->cursor, node.text);
        return buster_aarch64_syntax_match_children(node.child_first, node.child_count, state, optional_depth);
    }
    if (node.kind == BUSTER_AARCH64_SYNTAX_LIT)
        return buster_aarch64_syntax_input_take(state->input, &state->cursor, node.text);
    if (node.kind == BUSTER_AARCH64_SYNTAX_ANCHOR) return buster_aarch64_syntax_match_anchor(index, state);
    if (node.kind == BUSTER_AARCH64_SYNTAX_ALT) return buster_aarch64_syntax_match_alt(index, node, state, optional_depth);
    if (node.kind == BUSTER_AARCH64_SYNTAX_OPTIONAL)
    {
        if (optional_depth >= 2) return false;
        u64 cursor = state->cursor;
        u32 anchor_index = state->anchor_index;
        u64 token = buster_aarch64_syntax_callback_checkpoint(state->callbacks);
        if (buster_aarch64_syntax_match_children(node.child_first, node.child_count, state, optional_depth + 1)) return true;
        state->cursor = cursor;
        state->anchor_index = anchor_index;
        buster_aarch64_syntax_callback_restore(state->callbacks, token);
        return true;
    }
    return false;
}

BUSTER_F_DECL bool buster_aarch64_syntax_match_row(u32 row_index, String8 input, BusterAarch64SyntaxCallbacks callbacks)
{
    BusterAarch64SyntaxRow row = {0};
    if (!buster_aarch64_syntax_row(row_index, &row) || (!input.pointer && input.length) || !callbacks.match_anchor ||
        !buster_aarch64_syntax_callbacks_transactional(callbacks)) return false;
    BusterAarch64SyntaxMatchState state = {.input = input, .callbacks = callbacks};
    u64 initial_token = buster_aarch64_syntax_callback_checkpoint(callbacks);
    bool matched = buster_aarch64_syntax_match_node(row.node_first, &state, 0);
    if (!matched || state.cursor != input.length || state.anchor_index < row.anchor_min || state.anchor_index > row.anchor_max)
    {
        buster_aarch64_syntax_callback_restore(callbacks, initial_token);
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_output_append(BusterAarch64SyntaxOutput* output, String8 text)
{
    if (!output || (!text.pointer && text.length) || output->length > output->capacity || text.length > output->capacity - output->length) return false;
    if (text.length) memcpy(output->pointer + output->length, text.pointer, (size_t)text.length);
    output->length += text.length;
    return true;
}

/* Output traversal is kept separate from matching so a print callback cannot
 * accidentally observe a cursor or mutate matcher state. */
BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_node_impl(u32 index, BusterAarch64SyntaxPrintState* state);

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_children_range(u32 first, u32 count,
                                                                      BusterAarch64SyntaxPrintState* state)
{
    if (!buster_aarch64_syntax_child_range_valid(first, count)) return false;
    for (u32 print_offset = 0; print_offset < count; print_offset += 1)
    {
        u32 child = 0;
        if (!buster_aarch64_syntax_generated_child_at(first + print_offset, &child) ||
            !buster_aarch64_syntax_print_node_impl(child, state)) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_children(u32 first, u32 count, BusterAarch64SyntaxPrintState* state)
{
    return buster_aarch64_syntax_print_children_range(first, count, state);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_node_impl(u32 index, BusterAarch64SyntaxPrintState* state)
{
    BusterAarch64SyntaxNode node = {0};
    if (!state || !buster_aarch64_syntax_node(index, &node)) return false;
    if (node.kind == BUSTER_AARCH64_SYNTAX_LIT || node.kind == BUSTER_AARCH64_SYNTAX_MNEMONIC)
        return buster_aarch64_syntax_output_append(state->output, node.text);
    if (node.kind == BUSTER_AARCH64_SYNTAX_ANCHOR)
    {
        if (!state->callbacks.print_anchor || state->anchor_index == UINT32_MAX) return false;
        BusterAarch64SyntaxAnchor anchor = {.node_index = index, .occurrence = state->anchor_index, .flags = node.flags, .spelling = node.text};
        String8 spelling = {0};
        u64 token = buster_aarch64_syntax_callback_checkpoint(state->callbacks);
        if (!state->callbacks.print_anchor(state->callbacks.user, anchor, &spelling))
        {
            buster_aarch64_syntax_callback_restore(state->callbacks, token);
            return false;
        }
        state->anchor_index += 1;
        if (state->concrete) return buster_aarch64_syntax_output_append(state->output, spelling);
        if (spelling.length >= 2 && spelling.pointer[0] == '<' && spelling.pointer[spelling.length - 1] == '>')
            return buster_aarch64_syntax_output_append(state->output, spelling);
        return buster_aarch64_syntax_output_append(state->output, S8("<")) &&
               buster_aarch64_syntax_output_append(state->output, spelling) &&
               buster_aarch64_syntax_output_append(state->output, S8(">"));
    }
    if (node.kind == BUSTER_AARCH64_SYNTAX_ALT && state->concrete)
    {
        if (!state->callbacks.select_alternative) return false;
        u32 branch_count = 1;
        for (u32 offset = 0; offset < node.child_count; offset += 1)
        {
            u32 child = 0;
            BusterAarch64SyntaxNode separator = {0};
            if (!buster_aarch64_syntax_generated_child_at(node.child_first + offset, &child) ||
                !buster_aarch64_syntax_node(child, &separator)) return false;
            if (separator.kind == BUSTER_AARCH64_SYNTAX_LIT && buster_aarch64_syntax_bytes_equal(separator.text, S8("|"))) branch_count += 1;
        }
        u32 selected = 0;
        u64 token = buster_aarch64_syntax_callback_checkpoint(state->callbacks);
        if (!state->callbacks.select_alternative(state->callbacks.user, node, branch_count, &selected) || selected >= branch_count)
        {
            buster_aarch64_syntax_callback_restore(state->callbacks, token);
            return false;
        }
        u32 branch = 0;
        u32 branch_start = 0;
        for (u32 offset = 0; offset <= node.child_count; offset += 1)
        {
            bool separator_at_offset = offset == node.child_count;
            u32 child = 0;
            BusterAarch64SyntaxNode separator = {0};
            if (!separator_at_offset)
            {
                if (!buster_aarch64_syntax_generated_child_at(node.child_first + offset, &child) ||
                    !buster_aarch64_syntax_node(child, &separator)) return false;
                separator_at_offset = separator.kind == BUSTER_AARCH64_SYNTAX_LIT && buster_aarch64_syntax_bytes_equal(separator.text, S8("|"));
            }
            if (separator_at_offset)
            {
                if (branch == selected && !buster_aarch64_syntax_print_children_range(node.child_first + branch_start,
                                                                                         offset - branch_start, state)) return false;
                branch += 1;
                branch_start = offset + 1;
            }
        }
        return true;
    }
    if (node.kind == BUSTER_AARCH64_SYNTAX_OPTIONAL && state->concrete)
    {
        if (!state->callbacks.select_optional) return false;
        bool present = false;
        u64 token = buster_aarch64_syntax_callback_checkpoint(state->callbacks);
        if (!state->callbacks.select_optional(state->callbacks.user, node, &present))
        {
            buster_aarch64_syntax_callback_restore(state->callbacks, token);
            return false;
        }
        if (!present) return true;
        u32 first = node.child_first;
        u32 count = node.child_count;
        if (count)
        {
            u32 child = 0;
            BusterAarch64SyntaxNode delimiter = {0};
            if (!buster_aarch64_syntax_generated_child_at(first, &child) || !buster_aarch64_syntax_node(child, &delimiter)) return false;
            if (delimiter.kind == BUSTER_AARCH64_SYNTAX_LIT && (delimiter.flags & BUSTER_AARCH64_SYNTAX_LIT_DELIMITER))
            {
                first += 1;
                count -= 1;
            }
        }
        if (count)
        {
            u32 child = 0;
            BusterAarch64SyntaxNode delimiter = {0};
            if (!buster_aarch64_syntax_generated_child_at(first + count - 1, &child) || !buster_aarch64_syntax_node(child, &delimiter)) return false;
            if (delimiter.kind == BUSTER_AARCH64_SYNTAX_LIT && (delimiter.flags & BUSTER_AARCH64_SYNTAX_LIT_DELIMITER)) count -= 1;
        }
        return buster_aarch64_syntax_print_children_range(first, count, state);
    }
    if (node.kind == BUSTER_AARCH64_SYNTAX_ALT && !(node.flags & BUSTER_AARCH64_SYNTAX_ALT_IMPLICIT_DELIMITER) &&
        !buster_aarch64_syntax_output_append(state->output, S8("("))) return false;
    if (!buster_aarch64_syntax_print_children(node.child_first, node.child_count, state)) return false;
    if (node.kind == BUSTER_AARCH64_SYNTAX_ALT && !(node.flags & BUSTER_AARCH64_SYNTAX_ALT_IMPLICIT_DELIMITER) &&
        !buster_aarch64_syntax_output_append(state->output, S8(")"))) return false;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_row_mode(u32 row_index, BusterAarch64SyntaxCallbacks callbacks,
                                                               BusterAarch64SyntaxOutput* output, bool concrete)
{
    BusterAarch64SyntaxRow row = {0};
    if (!output || !buster_aarch64_syntax_row(row_index, &row) || !callbacks.print_anchor ||
        (!output->pointer && output->capacity) || !buster_aarch64_syntax_callbacks_transactional(callbacks)) return false;
    u64 original_length = output->length;
    u64 initial_token = buster_aarch64_syntax_callback_checkpoint(callbacks);
    BusterAarch64SyntaxPrintState state = {.output = output, .concrete = concrete, .callbacks = callbacks};
    if (!buster_aarch64_syntax_print_node_impl(row.node_first, &state) ||
        (concrete ? (state.anchor_index < row.anchor_min || state.anchor_index > row.anchor_max) : state.anchor_index != row.anchor_count))
    {
        output->length = original_length;
        buster_aarch64_syntax_callback_restore(callbacks, initial_token);
        return false;
    }
    return true;
}

BUSTER_F_DECL bool buster_aarch64_syntax_print_row(u32 row_index, BusterAarch64SyntaxCallbacks callbacks,
                                                    BusterAarch64SyntaxOutput* output)
{
    return buster_aarch64_syntax_print_row_mode(row_index, callbacks, output, false);
}

BUSTER_F_DECL bool buster_aarch64_syntax_print_concrete_row(u32 row_index, BusterAarch64SyntaxCallbacks callbacks,
                                                             BusterAarch64SyntaxOutput* output)
{
    if (!callbacks.select_alternative || !callbacks.select_optional) return false;
    return buster_aarch64_syntax_print_row_mode(row_index, callbacks, output, true);
}

BUSTER_F_DECL bool buster_aarch64_syntax_validate(void)
{
    BusterAarch64SyntaxCounts counts = buster_aarch64_syntax_counts();
    if (counts.row_count != 1695 || counts.canonical_row_count != 1523 || counts.alias_row_count != 172 ||
        counts.optional_node_count != 366 || counts.alt_node_count != 33 || counts.mem_node_count != 625 ||
        counts.mem_writeback_count != 36 || counts.list_node_count != 158 || counts.lane_node_count != 157 ||
        counts.anchor_occurrence_count != 6213 || counts.anchor_alternative_count != 680 || counts.range_anchor_count != 28 ||
        counts.mnemonic_optional_suffix_count != 55 || counts.mnemonic_condition_count != 1 ||
        counts.fixed_numeric_literal_count != 273)
        return false;
    for (u32 row_index = 0; row_index < counts.row_count; row_index += 1)
    {
        BusterAarch64SyntaxRow row = {0};
        if (!buster_aarch64_syntax_row(row_index, &row) || !buster_aarch64_syntax_node_range_valid(row.node_first, row.node_count)) return false;
        u32 anchors = 0;
        for (u32 validation_offset = 0; validation_offset < row.node_count; validation_offset += 1)
        {
            BusterAarch64SyntaxNode node = {0};
            if (!buster_aarch64_syntax_node(row.node_first + validation_offset, &node)) return false;
            if (node.kind == BUSTER_AARCH64_SYNTAX_ANCHOR) anchors += 1;
            if (!buster_aarch64_syntax_child_range_valid(node.child_first, node.child_count)) return false;
        }
        if (anchors != row.anchor_count) return false;
    }
    return true;
}
