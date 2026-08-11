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

typedef enum BusterAarch64SyntaxMatchTaskKind
{
    BUSTER_AARCH64_SYNTAX_MATCH_TASK_NODE,
    BUSTER_AARCH64_SYNTAX_MATCH_TASK_LITERAL,
} BusterAarch64SyntaxMatchTaskKind;

typedef struct BusterAarch64SyntaxMatchTask BusterAarch64SyntaxMatchTask;
struct BusterAarch64SyntaxMatchTask
{
    BusterAarch64SyntaxMatchTaskKind kind;
    u32 node_index;
    String8 literal;
    bool mnemonic;
};

typedef enum BusterAarch64SyntaxMatchBacktrackKind
{
    BUSTER_AARCH64_SYNTAX_MATCH_BACKTRACK_ALT,
    BUSTER_AARCH64_SYNTAX_MATCH_BACKTRACK_OPTIONAL,
    BUSTER_AARCH64_SYNTAX_MATCH_BACKTRACK_ANCHOR,
} BusterAarch64SyntaxMatchBacktrackKind;

typedef struct BusterAarch64SyntaxMatchBacktrack BusterAarch64SyntaxMatchBacktrack;
struct BusterAarch64SyntaxMatchBacktrack
{
    BusterAarch64SyntaxMatchBacktrackKind kind;
    u64 cursor;
    u32 capture_count;
    u32 choice_count;
    u32 task_count;
    u32 node_index;
    u32 next_branch;
    u32 branch_count;
    u64 next_cursor;
    u64 minimum_cursor;
};

typedef struct BusterAarch64SyntaxMatchMachine BusterAarch64SyntaxMatchMachine;
struct BusterAarch64SyntaxMatchMachine
{
    String8 input;
    u64 cursor;
    u32 capture_count;
    u32 choice_count;
    u32 task_count;
    u32 backtrack_count;
    BusterAarch64SyntaxCapture captures[BUSTER_AARCH64_SYNTAX_GENERATED_MAX_ANCHOR_OPERANDS];
    BusterAarch64SyntaxChoice choices[BUSTER_AARCH64_SYNTAX_GENERATED_MAX_CHOICE_COUNT];
    BusterAarch64SyntaxMatchTask tasks[BUSTER_AARCH64_SYNTAX_GENERATED_MAX_WORK_ITEMS];
    BusterAarch64SyntaxMatchBacktrack backtracks[BUSTER_AARCH64_SYNTAX_GENERATED_MAX_BACKTRACK_FRAMES];
};

typedef enum BusterAarch64SyntaxPrintTaskKind
{
    BUSTER_AARCH64_SYNTAX_PRINT_TASK_NODE,
    BUSTER_AARCH64_SYNTAX_PRINT_TASK_LITERAL,
} BusterAarch64SyntaxPrintTaskKind;

typedef struct BusterAarch64SyntaxPrintTask BusterAarch64SyntaxPrintTask;
struct BusterAarch64SyntaxPrintTask
{
    BusterAarch64SyntaxPrintTaskKind kind;
    u32 node_index;
    String8 literal;
    bool mnemonic;
};

typedef struct BusterAarch64SyntaxPrintMachine BusterAarch64SyntaxPrintMachine;
struct BusterAarch64SyntaxPrintMachine
{
    BusterAarch64SyntaxPrintRequest request;
    bool concrete;
    char8* output;
    u64 capacity;
    u64 length;
    u32 anchor_count;
    u32 choice_count;
    u32 task_count;
    BusterAarch64SyntaxPrintTask tasks[BUSTER_AARCH64_SYNTAX_GENERATED_MAX_WORK_ITEMS];
};

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

BUSTER_GLOBAL_LOCAL u8 buster_aarch64_syntax_ascii_fold(u8 value)
{
    return value >= 'a' && value <= 'z' ? (u8)(value - 'a' + 'A') : value;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_ascii_bytes_equal(String8 left, String8 right)
{
    if (left.length != right.length || (!left.pointer && left.length) || (!right.pointer && right.length)) return false;
    for (u64 index = 0; index < left.length; index += 1)
    {
        if (buster_aarch64_syntax_ascii_fold((u8)left.pointer[index]) !=
            buster_aarch64_syntax_ascii_fold((u8)right.pointer[index])) return false;
    }
    return true;
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

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_node_at(u32 index, BusterAarch64SyntaxGeneratedNode* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT ||
        index > (UINT32_MAX - 20u) / BUSTER_AARCH64_SYNTAX_GENERATED_NODE_RECORD_BYTES) return false;
    u8 bytes[20] = {0};
    if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_nodes_blob,
                                            (u32)(sizeof(buster_aarch64_syntax_generated_nodes_blob) - 1),
                                            index * BUSTER_AARCH64_SYNTAX_GENERATED_NODE_RECORD_BYTES, bytes, sizeof(bytes))) return false;
    *result = (BusterAarch64SyntaxGeneratedNode){
        .child_first = buster_aarch64_syntax_read_u32(bytes + 0),
        .child_count = buster_aarch64_syntax_read_u32(bytes + 4),
        .text_offset = buster_aarch64_syntax_read_u32(bytes + 8),
        .text_length = buster_aarch64_syntax_read_u32(bytes + 12),
        .kind_flags = buster_aarch64_syntax_read_u16(bytes + 16),
        .numeric_count = buster_aarch64_syntax_read_u16(bytes + 18),
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_row_at(u32 index, BusterAarch64SyntaxGeneratedRow* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT ||
        index > (UINT32_MAX - BUSTER_AARCH64_SYNTAX_GENERATED_ROW_RECORD_BYTES) / BUSTER_AARCH64_SYNTAX_GENERATED_ROW_RECORD_BYTES) return false;
    u8 bytes[64] = {0};
    if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_rows_blob,
                                            (u32)(sizeof(buster_aarch64_syntax_generated_rows_blob) - 1),
                                            index * BUSTER_AARCH64_SYNTAX_GENERATED_ROW_RECORD_BYTES, bytes, sizeof(bytes))) return false;
    *result = (BusterAarch64SyntaxGeneratedRow){
        .node_first = buster_aarch64_syntax_read_u32(bytes + 0),
        .node_count = buster_aarch64_syntax_read_u32(bytes + 4),
        .id_offset = buster_aarch64_syntax_read_u32(bytes + 8),
        .id_length = buster_aarch64_syntax_read_u32(bytes + 12),
        .assembly_offset = buster_aarch64_syntax_read_u32(bytes + 16),
        .assembly_length = buster_aarch64_syntax_read_u32(bytes + 20),
        .mnemonic_offset = buster_aarch64_syntax_read_u32(bytes + 24),
        .mnemonic_length = buster_aarch64_syntax_read_u32(bytes + 28),
        .encoding_offset = buster_aarch64_syntax_read_u32(bytes + 32),
        .encoding_length = buster_aarch64_syntax_read_u32(bytes + 36),
        .anchor_count = buster_aarch64_syntax_read_u32(bytes + 40),
        .anchor_min = buster_aarch64_syntax_read_u32(bytes + 44),
        .anchor_max = buster_aarch64_syntax_read_u32(bytes + 48),
        .row_kind = buster_aarch64_syntax_read_u32(bytes + 52),
        .source_hash = buster_aarch64_syntax_read_u64(bytes + 56),
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_child_at(u32 index, u32* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_COUNT ||
        index > (UINT32_MAX - BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_RECORD_BYTES) /
                    BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_RECORD_BYTES) return false;
    u8 bytes[4] = {0};
    if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_child_indices_blob,
                                            (u32)(sizeof(buster_aarch64_syntax_generated_child_indices_blob) - 1),
                                            index * BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_RECORD_BYTES, bytes, sizeof(bytes))) return false;
    *result = buster_aarch64_syntax_read_u32(bytes);
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_range_at(u32 index, BusterAarch64SyntaxGeneratedMnemonicRange* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_COUNT ||
        index > (UINT32_MAX - BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_RECORD_BYTES) /
                    BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_RECORD_BYTES) return false;
    u8 bytes[16] = {0};
    if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_mnemonic_ranges_blob,
                                            (u32)(sizeof(buster_aarch64_syntax_generated_mnemonic_ranges_blob) - 1),
                                            index * BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_RECORD_BYTES, bytes, sizeof(bytes))) return false;
    *result = (BusterAarch64SyntaxGeneratedMnemonicRange){
        .key_offset = buster_aarch64_syntax_read_u32(bytes + 0),
        .key_length = buster_aarch64_syntax_read_u32(bytes + 4),
        .candidate_first = buster_aarch64_syntax_read_u32(bytes + 8),
        .candidate_count = buster_aarch64_syntax_read_u32(bytes + 12),
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_candidate_at(u32 index, u32* result)
{
    if (!result || index >= BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT ||
        index > (UINT32_MAX - BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_RECORD_BYTES) /
                    BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_RECORD_BYTES) return false;
    u8 bytes[4] = {0};
    if (!buster_aarch64_syntax_packed_bytes(buster_aarch64_syntax_generated_mnemonic_candidates_blob,
                                            (u32)(sizeof(buster_aarch64_syntax_generated_mnemonic_candidates_blob) - 1),
                                            index * BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_RECORD_BYTES, bytes, sizeof(bytes))) return false;
    *result = buster_aarch64_syntax_read_u32(bytes);
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

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_row_fields_valid(BusterAarch64SyntaxGeneratedRow row);

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_row_valid(u32 index)
{
    if (index >= BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT) return false;
    BusterAarch64SyntaxGeneratedRow row = {0};
    if (!buster_aarch64_syntax_generated_row_at(index, &row)) return false;
    return buster_aarch64_syntax_generated_row_fields_valid(row);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_generated_row_fields_valid(BusterAarch64SyntaxGeneratedRow row)
{
    return row.node_first <= BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT &&
           row.node_count <= BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT - row.node_first &&
           buster_aarch64_syntax_pool_range_valid(row.id_offset, row.id_length) &&
           buster_aarch64_syntax_pool_range_valid(row.assembly_offset, row.assembly_length) &&
           buster_aarch64_syntax_pool_range_valid(row.mnemonic_offset, row.mnemonic_length) &&
           buster_aarch64_syntax_pool_range_valid(row.encoding_offset, row.encoding_length) &&
           row.anchor_min <= row.anchor_max && row.anchor_max <= row.anchor_count;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL bool buster_aarch64_syntax_test_generated_row_fields_valid(u32 index, u32 encoding_offset,
                                                                          u32 encoding_length)
{
    BusterAarch64SyntaxGeneratedRow row = {0};
    if (!buster_aarch64_syntax_generated_row_at(index, &row)) return false;
    row.encoding_offset = encoding_offset;
    row.encoding_length = encoding_length;
    return buster_aarch64_syntax_generated_row_fields_valid(row);
}
#endif

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
        .encoding_name = buster_aarch64_syntax_pool_string(row.encoding_offset, row.encoding_length),
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
        .generic_shape_count = BUSTER_AARCH64_SYNTAX_GENERATED_GENERIC_SHAPE_COUNT,
        .exact_shape_count = BUSTER_AARCH64_SYNTAX_GENERATED_EXACT_SHAPE_COUNT,
        .max_total_ast_nodes = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_TOTAL_AST_NODES,
        .max_non_lit_non_seq_nodes = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_NON_LIT_NON_SEQ_NODES,
        .max_optional_depth = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_OPTIONAL_DEPTH,
        .max_delimiter_nesting = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_DELIMITER_NESTING,
        .max_top_level_comma_groups = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_TOP_LEVEL_COMMA_GROUPS,
        .max_anchor_operands = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_ANCHOR_OPERANDS,
        .max_choice_count = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_CHOICE_COUNT,
        .max_assembly_bytes = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_ASSEMBLY_BYTES,
        .max_work_items = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_WORK_ITEMS,
        .max_backtrack_frames = BUSTER_AARCH64_SYNTAX_GENERATED_MAX_BACKTRACK_FRAMES,
        .input_digest_hi = UINT64_C(0xeea16d7f094badc6),
        .input_digest_lo = UINT64_C(0x5614aed988621f48),
    };
}

BUSTER_F_DECL String8 buster_aarch64_syntax_source_digest(void)
{
    return S8(BUSTER_AARCH64_SYNTAX_GENERATED_SOURCE_SHA256);
}
BUSTER_F_DECL String8 buster_aarch64_syntax_input_digest(void)
{
    return S8(BUSTER_AARCH64_SYNTAX_GENERATED_INPUT_DIGEST);
}
BUSTER_F_DECL String8 buster_aarch64_syntax_generic_shape_digest(void)
{
    return S8(BUSTER_AARCH64_SYNTAX_GENERATED_GENERIC_SHAPE_DIGEST);
}
BUSTER_F_DECL String8 buster_aarch64_syntax_generic_row_digest(void)
{
    return S8(BUSTER_AARCH64_SYNTAX_GENERATED_GENERIC_ROW_DIGEST);
}
BUSTER_F_DECL String8 buster_aarch64_syntax_exact_shape_digest(void)
{
    return S8(BUSTER_AARCH64_SYNTAX_GENERATED_EXACT_SHAPE_DIGEST);
}
BUSTER_F_DECL String8 buster_aarch64_syntax_exact_row_digest(void)
{
    return S8(BUSTER_AARCH64_SYNTAX_GENERATED_EXACT_ROW_DIGEST);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_input_take(String8 input, u64* cursor, String8 literal)
{
    if (!cursor || *cursor > input.length || literal.length > input.length - *cursor ||
        (!input.pointer && input.length) || (!literal.pointer && literal.length)) return false;
    if (!literal.length) return true;
    if (!buster_aarch64_syntax_ascii_bytes_equal((String8){.pointer = input.pointer + *cursor, .length = literal.length}, literal)) return false;
    *cursor += literal.length;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_input_take_mnemonic(String8 input, u64* cursor, String8 literal)
{
    return buster_aarch64_syntax_input_take(input, cursor, literal);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_push_task(BusterAarch64SyntaxMatchMachine* machine,
                                                                BusterAarch64SyntaxMatchTask task)
{
    if (!machine || machine->task_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_WORK_ITEMS) return false;
    machine->tasks[machine->task_count] = task;
    machine->task_count += 1;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_push_node(BusterAarch64SyntaxMatchMachine* machine, u32 node_index)
{
    return buster_aarch64_syntax_match_push_task(machine, (BusterAarch64SyntaxMatchTask){
        .kind = BUSTER_AARCH64_SYNTAX_MATCH_TASK_NODE, .node_index = node_index});
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_push_literal(BusterAarch64SyntaxMatchMachine* machine, String8 literal,
                                                                    bool mnemonic)
{
    return buster_aarch64_syntax_match_push_task(machine, (BusterAarch64SyntaxMatchTask){
        .kind = BUSTER_AARCH64_SYNTAX_MATCH_TASK_LITERAL, .literal = literal, .mnemonic = mnemonic});
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_push_children_reverse(BusterAarch64SyntaxMatchMachine* machine,
                                                                              u32 first, u32 count)
{
    if (!machine || !buster_aarch64_syntax_child_range_valid(first, count)) return false;
    for (u32 offset = count; offset > 0; offset -= 1)
    {
        u32 child = 0;
        if (!buster_aarch64_syntax_generated_child_at(first + offset - 1, &child) ||
            !buster_aarch64_syntax_match_push_node(machine, child)) return false;
    }
    return true;
}

/* Optional groups carry their source-template braces as delimiter literals.
   They are part of the display spelling, but concrete assembly omits those
   notation braces.  Select the form by peeking at the next input byte, while
   leaving the cursor untouched so the normal task machine remains
   transactional. */
BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_push_optional_body(BusterAarch64SyntaxMatchMachine* machine,
                                                                          BusterAarch64SyntaxNode node)
{
    if (!machine || !buster_aarch64_syntax_child_range_valid(node.child_first, node.child_count)) return false;
    u32 first = node.child_first;
    u32 count = node.child_count;
    if (!count) return true;

    u32 child = 0;
    BusterAarch64SyntaxNode delimiter = {0};
    bool has_delimiters = false;
    if (buster_aarch64_syntax_generated_child_at(first, &child) && buster_aarch64_syntax_node(child, &delimiter) &&
        delimiter.kind == BUSTER_AARCH64_SYNTAX_LIT && (delimiter.flags & BUSTER_AARCH64_SYNTAX_LIT_DELIMITER))
    {
        has_delimiters = true;
    }

    bool display_form = true;
    if (has_delimiters)
    {
        display_form = machine->cursor <= machine->input.length && delimiter.text.length <= machine->input.length - machine->cursor &&
                       buster_aarch64_syntax_ascii_bytes_equal(
                           (String8){.pointer = machine->input.pointer + machine->cursor, .length = delimiter.text.length},
                           delimiter.text);
    }
    if (!display_form)
    {
        first += 1;
        count -= 1;
        if (count)
        {
            if (!buster_aarch64_syntax_generated_child_at(first + count - 1, &child) ||
                !buster_aarch64_syntax_node(child, &delimiter)) return false;
            if (delimiter.kind == BUSTER_AARCH64_SYNTAX_LIT && (delimiter.flags & BUSTER_AARCH64_SYNTAX_LIT_DELIMITER)) count -= 1;
        }
    }
    return buster_aarch64_syntax_match_push_children_reverse(machine, first, count);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_node_is_separator(BusterAarch64SyntaxNode node)
{
    return node.kind == BUSTER_AARCH64_SYNTAX_LIT && buster_aarch64_syntax_bytes_equal(node.text, S8("|"));
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_branch_range(BusterAarch64SyntaxNode node, u32 target,
                                                              u32* first, u32* count, u32* total)
{
    u32 branch = 0;
    u32 branch_first = 0;
    u32 branch_count = 0;
    bool found = false;
    for (u32 offset = 0; offset <= node.child_count; offset += 1)
    {
        bool separator = offset == node.child_count;
        if (!separator)
        {
            u32 child = 0;
            BusterAarch64SyntaxNode child_node = {0};
            separator = buster_aarch64_syntax_generated_child_at(node.child_first + offset, &child) &&
                        buster_aarch64_syntax_node(child, &child_node) &&
                        buster_aarch64_syntax_node_is_separator(child_node);
        }
        if (separator)
        {
            if (branch == target)
            {
                if (first) *first = node.child_first + branch_first;
                if (count) *count = branch_count;
                found = true;
            }
            branch += 1;
            branch_first = offset + 1;
            branch_count = 0;
        }
        else
        {
            branch_count += 1;
        }
    }
    if (total) *total = branch;
    return found;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_input_has_implicit_separator(String8 input, u64 cursor)
{
    if (!input.pointer || cursor > input.length) return false;
    for (u64 index = cursor; index < input.length && input.pointer[index] != '}'; index += 1)
    {
        if (input.pointer[index] == '<')
        {
            while (index < input.length && input.pointer[index] != '>') index += 1;
        }
        else if (input.pointer[index] == '|')
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_record_choice(BusterAarch64SyntaxMatchMachine* machine,
                                                                     u32 node_index, u32 value)
{
    if (!machine || machine->choice_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_CHOICE_COUNT) return false;
    machine->choices[machine->choice_count++] = (BusterAarch64SyntaxChoice){.node_index = node_index, .value = value};
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_capture_span(BusterAarch64SyntaxMatchMachine* machine, u32 node_index,
                                                                    u64 start, u64 end)
{
    if (!machine || !machine->input.pointer || start >= end || end > machine->input.length ||
        machine->capture_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_ANCHOR_OPERANDS)
        return false;
    u32 occurrence = machine->capture_count;
    machine->captures[occurrence] = (BusterAarch64SyntaxCapture){
        .node_index = node_index,
        .occurrence = occurrence,
        .spelling = {.pointer = machine->input.pointer + start, .length = end - start},
    };
    machine->capture_count = occurrence + 1;
    machine->cursor = end;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_anchor_end(BusterAarch64SyntaxMatchMachine* machine, u64 start, u64* end,
                                                                  bool* splittable)
{
    if (!machine || !end || !splittable || !machine->input.pointer || start >= machine->input.length) return false;
    u64 cursor = start;
    *splittable = machine->input.pointer[start] != '<';
    if (!*splittable)
    {
        cursor += 1;
        while (cursor < machine->input.length && machine->input.pointer[cursor] != '>') cursor += 1;
        if (cursor >= machine->input.length) return false;
        *end = cursor + 1;
        return true;
    }
    while (cursor < machine->input.length)
    {
        char8 character = machine->input.pointer[cursor];
        if (character == ' ' || character == '\t' || character == ',' || character == '[' || character == ']' ||
            character == '{' || character == '}' || character == '(' || character == ')' || character == '|' ||
            character == '!' || character == '.')
            break;
        cursor += 1;
    }
    if (cursor == start) return false;
    *end = cursor;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_anchor_is_vector_composite(BusterAarch64SyntaxMatchMachine* machine,
                                                                                  u32 node_index, u64 start,
                                                                                  BusterAarch64SyntaxNode node,
                                                                                  bool* vector_prefix)
{
    /* Some Arm SIMD rows decompose an arranged register into adjacent
     * prefix/index anchors (for example <V><d>).  A scalar prefix (s0/h0)
     * remains an ordinary adjacent pair; only a vector prefix (v0.4s) owns
     * the arrangement suffix on its index capture. */
    if (!machine || !vector_prefix || machine->capture_count == 0 || node.kind != BUSTER_AARCH64_SYNTAX_ANCHOR ||
        node.text.length != 1 || (node.text.pointer[0] != 'd' && node.text.pointer[0] != 'n' && node.text.pointer[0] != 'm'))
        return false;
    BusterAarch64SyntaxCapture previous_capture = machine->captures[machine->capture_count - 1];
    BusterAarch64SyntaxNode previous_node = {0};
    if (!buster_aarch64_syntax_node(previous_capture.node_index, &previous_node) ||
        previous_node.kind != BUSTER_AARCH64_SYNTAX_ANCHOR || previous_node.text.length < 1 || previous_node.text.pointer[0] != 'V' ||
        !previous_capture.spelling.pointer || previous_capture.spelling.pointer + previous_capture.spelling.length !=
                                                      machine->input.pointer + start)
        return false;
    *vector_prefix = previous_capture.spelling.length == 1 &&
                     buster_aarch64_syntax_ascii_fold((u8)previous_capture.spelling.pointer[0]) == 'V';
    BUSTER_UNUSED(node_index);
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_anchor_vector_suffix(BusterAarch64SyntaxMatchMachine* machine, u64 start,
                                                                            u64 base_end, u64* end)
{
    if (!machine || !end || !machine->input.pointer || base_end >= machine->input.length ||
        machine->input.pointer[base_end] != '.')
        return false;
    u64 cursor = base_end + 1;
    bool alphanumeric = false;
    while (cursor < machine->input.length)
    {
        char8 character = machine->input.pointer[cursor];
        if (character == ' ' || character == '\t' || character == ',' || character == '[' || character == ']' ||
            character == '{' || character == '}' || character == '(' || character == ')' || character == '|' ||
            character == '!' || character == '.')
            break;
        alphanumeric |= (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
                        (character >= 'A' && character <= 'Z');
        cursor += 1;
    }
    if (cursor == base_end + 1 || !alphanumeric || cursor <= start) return false;
    *end = cursor;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_anchor(BusterAarch64SyntaxMatchMachine* machine, u32 node_index,
                                                              BusterAarch64SyntaxNode node)
{
    if (!machine || machine->capture_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_ANCHOR_OPERANDS) return false;
    u64 start = machine->cursor;
    u64 end = 0;
    bool splittable = false;
    if (!buster_aarch64_syntax_match_anchor_end(machine, start, &end, &splittable)) return false;
    bool vector_composite = false;
    bool vector_prefix = false;
    vector_composite = buster_aarch64_syntax_match_anchor_is_vector_composite(machine, node_index, start, node, &vector_prefix);
    if (vector_composite && vector_prefix)
    {
        u64 suffix_end = 0;
        if (!buster_aarch64_syntax_match_anchor_vector_suffix(machine, start, end, &suffix_end)) return false;
        end = suffix_end;
    }
    if (splittable && end > start + 1)
    {
        if (machine->backtrack_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_BACKTRACK_FRAMES) return false;
        machine->backtracks[machine->backtrack_count++] = (BusterAarch64SyntaxMatchBacktrack){
            .kind = BUSTER_AARCH64_SYNTAX_MATCH_BACKTRACK_ANCHOR,
            .cursor = start,
            .capture_count = machine->capture_count,
            .choice_count = machine->choice_count,
            .task_count = machine->task_count,
            .node_index = node_index,
            .next_cursor = end - 1,
            .minimum_cursor = start,
        };
    }
    BUSTER_UNUSED(node);
    return buster_aarch64_syntax_match_capture_span(machine, node_index, start, end);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_setup_branch(BusterAarch64SyntaxMatchMachine* machine,
                                                                    BusterAarch64SyntaxMatchBacktrack frame)
{
    BusterAarch64SyntaxNode node = {0};
    u32 first = 0;
    u32 count = 0;
    u32 total = 0;
    if (!machine || !buster_aarch64_syntax_node(frame.node_index, &node) ||
        !buster_aarch64_syntax_branch_range(node, frame.next_branch, &first, &count, &total) ||
        frame.next_branch >= total || !buster_aarch64_syntax_match_record_choice(machine, frame.node_index, frame.next_branch)) return false;
    if (frame.next_branch + 1 < total)
    {
        if (machine->backtrack_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_BACKTRACK_FRAMES) return false;
        frame.branch_count = total;
        frame.next_branch += 1;
        machine->backtracks[machine->backtrack_count++] = frame;
    }
    return buster_aarch64_syntax_match_push_children_reverse(machine, first, count);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_backtrack(BusterAarch64SyntaxMatchMachine* machine)
{
    while (machine && machine->backtrack_count)
    {
        BusterAarch64SyntaxMatchBacktrack frame = machine->backtracks[--machine->backtrack_count];
        machine->cursor = frame.cursor;
        machine->capture_count = frame.capture_count;
        machine->choice_count = frame.choice_count;
        machine->task_count = frame.task_count;
        if (frame.kind == BUSTER_AARCH64_SYNTAX_MATCH_BACKTRACK_ANCHOR)
        {
            if (frame.next_cursor > frame.minimum_cursor)
            {
                u64 candidate_end = frame.next_cursor;
                if (candidate_end > frame.minimum_cursor + 1)
                {
                    if (machine->backtrack_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_BACKTRACK_FRAMES) return false;
                    BusterAarch64SyntaxMatchBacktrack next = frame;
                    next.next_cursor = candidate_end - 1;
                    machine->backtracks[machine->backtrack_count++] = next;
                }
                if (buster_aarch64_syntax_match_capture_span(machine, frame.node_index, frame.cursor, candidate_end)) return true;
            }
        }
        else if (frame.kind == BUSTER_AARCH64_SYNTAX_MATCH_BACKTRACK_OPTIONAL)
        {
            if (buster_aarch64_syntax_match_record_choice(machine, frame.node_index, 0)) return true;
        }
        else if (frame.next_branch < frame.branch_count)
        {
            if (buster_aarch64_syntax_match_setup_branch(machine, frame)) return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_process_node(BusterAarch64SyntaxMatchMachine* machine, u32 index,
                                                                    BusterAarch64SyntaxNode node)
{
    if (node.kind == BUSTER_AARCH64_SYNTAX_LIT || node.kind == BUSTER_AARCH64_SYNTAX_MNEMONIC)
        return buster_aarch64_syntax_input_take_mnemonic(machine->input, &machine->cursor, node.text);
    if (node.kind == BUSTER_AARCH64_SYNTAX_ANCHOR) return buster_aarch64_syntax_match_anchor(machine, index, node);
    if (node.kind == BUSTER_AARCH64_SYNTAX_SEQ || node.kind == BUSTER_AARCH64_SYNTAX_MEM ||
        node.kind == BUSTER_AARCH64_SYNTAX_LIST || node.kind == BUSTER_AARCH64_SYNTAX_LANE)
        return buster_aarch64_syntax_match_push_children_reverse(machine, node.child_first, node.child_count);
    if (node.kind == BUSTER_AARCH64_SYNTAX_OPTIONAL)
    {
        if (!buster_aarch64_syntax_match_record_choice(machine, index, 1) ||
            machine->backtrack_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_BACKTRACK_FRAMES)
            return false;
        BusterAarch64SyntaxMatchBacktrack frame = {
            .kind = BUSTER_AARCH64_SYNTAX_MATCH_BACKTRACK_OPTIONAL,
            .cursor = machine->cursor,
            .capture_count = machine->capture_count,
            .choice_count = machine->choice_count - 1,
            .task_count = machine->task_count,
            .node_index = index,
        };
        machine->backtracks[machine->backtrack_count++] = frame;
        return buster_aarch64_syntax_match_push_optional_body(machine, node);
    }
    if (node.kind == BUSTER_AARCH64_SYNTAX_ALT)
    {
        bool delimited = !(node.flags & BUSTER_AARCH64_SYNTAX_ALT_IMPLICIT_DELIMITER);
        bool canonical = delimited ? (machine->cursor < machine->input.length && machine->input.pointer[machine->cursor] == '(') :
                                    buster_aarch64_syntax_input_has_implicit_separator(machine->input, machine->cursor);
        if (canonical)
        {
            if (delimited && !buster_aarch64_syntax_input_take(machine->input, &machine->cursor, S8("("))) return false;
            if (!buster_aarch64_syntax_match_record_choice(machine, index, BUSTER_AARCH64_SYNTAX_CHOICE_CANONICAL)) return false;
            if (delimited && !buster_aarch64_syntax_match_push_literal(machine, S8(")"), false)) return false;
            return buster_aarch64_syntax_match_push_children_reverse(machine, node.child_first, node.child_count);
        }
        u32 first = 0;
        u32 count = 0;
        u32 total = 0;
        if (!buster_aarch64_syntax_branch_range(node, 0, &first, &count, &total) || !total ||
            !buster_aarch64_syntax_match_record_choice(machine, index, 0)) return false;
        if (total > 1)
        {
            if (machine->backtrack_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_BACKTRACK_FRAMES) return false;
            machine->backtracks[machine->backtrack_count++] = (BusterAarch64SyntaxMatchBacktrack){
                .kind = BUSTER_AARCH64_SYNTAX_MATCH_BACKTRACK_ALT,
                .cursor = machine->cursor,
                .capture_count = machine->capture_count,
                .choice_count = machine->choice_count - 1,
                .task_count = machine->task_count,
                .node_index = index,
                .next_branch = 1,
                .branch_count = total,
            };
        }
        return buster_aarch64_syntax_match_push_children_reverse(machine, first, count);
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_match_process_task(BusterAarch64SyntaxMatchMachine* machine,
                                                                    BusterAarch64SyntaxMatchTask task)
{
    if (task.kind == BUSTER_AARCH64_SYNTAX_MATCH_TASK_LITERAL)
        return buster_aarch64_syntax_input_take_mnemonic(machine->input, &machine->cursor, task.literal);
    BusterAarch64SyntaxNode node = {0};
    return buster_aarch64_syntax_node(task.node_index, &node) &&
           buster_aarch64_syntax_match_process_node(machine, task.node_index, node);
}

BUSTER_F_DECL bool buster_aarch64_syntax_match_row(u32 row_index, String8 input, BusterAarch64SyntaxMatchResult* result)
{
    BusterAarch64SyntaxRow row = {0};
    if (!buster_aarch64_syntax_row(row_index, &row) || (!input.pointer && input.length) ||
        (result && result->capture_capacity && !result->captures) || (result && result->choice_capacity && !result->choices)) return false;
    BusterAarch64SyntaxMatchMachine machine = {.input = input};
    if (!buster_aarch64_syntax_match_push_node(&machine, row.node_first)) return false;
    bool matched = false;
    while (!matched)
    {
        if (!machine.task_count)
        {
            if (machine.cursor == input.length && machine.capture_count >= row.anchor_min && machine.capture_count <= row.anchor_max)
            {
                matched = true;
                break;
            }
            if (!buster_aarch64_syntax_match_backtrack(&machine)) return false;
            continue;
        }
        BusterAarch64SyntaxMatchTask task = machine.tasks[--machine.task_count];
        if (!buster_aarch64_syntax_match_process_task(&machine, task) && !buster_aarch64_syntax_match_backtrack(&machine)) return false;
    }
    if (result)
    {
        if (machine.capture_count > result->capture_capacity || machine.choice_count > result->choice_capacity) return false;
        for (u32 index = 0; index < machine.capture_count; index += 1) result->captures[index] = machine.captures[index];
        for (u32 index = 0; index < machine.choice_count; index += 1) result->choices[index] = machine.choices[index];
        result->capture_count = machine.capture_count;
        result->choice_count = machine.choice_count;
        result->consumed = machine.cursor;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_push_task(BusterAarch64SyntaxPrintMachine* machine,
                                                                BusterAarch64SyntaxPrintTask task)
{
    if (!machine || machine->task_count >= BUSTER_AARCH64_SYNTAX_GENERATED_MAX_WORK_ITEMS) return false;
    machine->tasks[machine->task_count++] = task;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_push_node(BusterAarch64SyntaxPrintMachine* machine, u32 node_index)
{
    return buster_aarch64_syntax_print_push_task(machine, (BusterAarch64SyntaxPrintTask){
        .kind = BUSTER_AARCH64_SYNTAX_PRINT_TASK_NODE, .node_index = node_index});
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_push_literal(BusterAarch64SyntaxPrintMachine* machine, String8 literal)
{
    return buster_aarch64_syntax_print_push_task(machine, (BusterAarch64SyntaxPrintTask){
        .kind = BUSTER_AARCH64_SYNTAX_PRINT_TASK_LITERAL, .literal = literal});
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_push_children_reverse(BusterAarch64SyntaxPrintMachine* machine,
                                                                              u32 first, u32 count)
{
    if (!machine || !buster_aarch64_syntax_child_range_valid(first, count)) return false;
    for (u32 offset = count; offset > 0; offset -= 1)
    {
        u32 child = 0;
        if (!buster_aarch64_syntax_generated_child_at(first + offset - 1, &child) ||
            !buster_aarch64_syntax_print_push_node(machine, child)) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_push_optional_body(BusterAarch64SyntaxPrintMachine* machine,
                                                                          BusterAarch64SyntaxNode node)
{
    if (!machine || !buster_aarch64_syntax_child_range_valid(node.child_first, node.child_count)) return false;
    u32 first = node.child_first;
    u32 count = node.child_count;
    if (machine->concrete && count)
    {
        u32 child = 0;
        BusterAarch64SyntaxNode delimiter = {0};
        if (!buster_aarch64_syntax_generated_child_at(first, &child) || !buster_aarch64_syntax_node(child, &delimiter)) return false;
        if (delimiter.kind == BUSTER_AARCH64_SYNTAX_LIT && (delimiter.flags & BUSTER_AARCH64_SYNTAX_LIT_DELIMITER))
        {
            first += 1;
            count -= 1;
        }
        if (count)
        {
            if (!buster_aarch64_syntax_generated_child_at(first + count - 1, &child) ||
                !buster_aarch64_syntax_node(child, &delimiter)) return false;
            if (delimiter.kind == BUSTER_AARCH64_SYNTAX_LIT && (delimiter.flags & BUSTER_AARCH64_SYNTAX_LIT_DELIMITER)) count -= 1;
        }
    }
    return buster_aarch64_syntax_print_push_children_reverse(machine, first, count);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_emit(BusterAarch64SyntaxPrintMachine* machine, String8 text)
{
    if (!machine || (!text.pointer && text.length) || text.length > UINT64_MAX - machine->length) return false;
    if (machine->output && (machine->length > machine->capacity || text.length > machine->capacity - machine->length)) return false;
    if (machine->output && text.length) memcpy(machine->output + machine->length, text.pointer, (size_t)text.length);
    machine->length += text.length;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_choice(BusterAarch64SyntaxPrintMachine* machine, u32 node_index, u32* value)
{
    if (!machine || !value) return false;
    bool found = false;
    for (u32 index = 0; index < machine->request.choice_count; index += 1)
    {
        BusterAarch64SyntaxChoice choice = machine->request.choices[index];
        if (choice.node_index == node_index)
        {
            if (found) return false;
            *value = choice.value;
            machine->choice_count += 1;
            found = true;
        }
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_branch(BusterAarch64SyntaxPrintMachine* machine,
                                                              BusterAarch64SyntaxNode node, u32 branch)
{
    u32 first = 0;
    u32 count = 0;
    u32 total = 0;
    if (!buster_aarch64_syntax_branch_range(node, branch, &first, &count, &total) || branch >= total) return false;
    return buster_aarch64_syntax_print_push_children_reverse(machine, first, count);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_process_node(BusterAarch64SyntaxPrintMachine* machine, u32 index,
                                                                    BusterAarch64SyntaxNode node)
{
    if (node.kind == BUSTER_AARCH64_SYNTAX_LIT || node.kind == BUSTER_AARCH64_SYNTAX_MNEMONIC)
        return buster_aarch64_syntax_print_emit(machine, node.text);
    if (node.kind == BUSTER_AARCH64_SYNTAX_ANCHOR)
    {
        u32 occurrence = machine->anchor_count++;
        if (!machine->concrete)
        {
            return buster_aarch64_syntax_print_emit(machine, S8("<")) &&
                   buster_aarch64_syntax_print_emit(machine, node.text) && buster_aarch64_syntax_print_emit(machine, S8(">"));
        }
        if (occurrence >= machine->request.capture_count) return false;
        BusterAarch64SyntaxCapture capture = machine->request.captures[occurrence];
        if (capture.node_index != index || capture.occurrence != occurrence) return false;
        return buster_aarch64_syntax_print_emit(machine, capture.spelling);
    }
    if (node.kind == BUSTER_AARCH64_SYNTAX_SEQ || node.kind == BUSTER_AARCH64_SYNTAX_MEM ||
        node.kind == BUSTER_AARCH64_SYNTAX_LIST || node.kind == BUSTER_AARCH64_SYNTAX_LANE)
        return buster_aarch64_syntax_print_push_children_reverse(machine, node.child_first, node.child_count);
    if (node.kind == BUSTER_AARCH64_SYNTAX_OPTIONAL)
    {
        if (!machine->concrete) return buster_aarch64_syntax_print_push_children_reverse(machine, node.child_first, node.child_count);
        u32 present = 0;
        if (!buster_aarch64_syntax_print_choice(machine, index, &present) || present > 1) return false;
        if (!present) return true;
        return buster_aarch64_syntax_print_push_optional_body(machine, node);
    }
    if (node.kind == BUSTER_AARCH64_SYNTAX_ALT)
    {
        bool delimited = !(node.flags & BUSTER_AARCH64_SYNTAX_ALT_IMPLICIT_DELIMITER);
        if (!machine->concrete)
        {
            if (delimited && !buster_aarch64_syntax_print_emit(machine, S8("("))) return false;
            if (delimited && !buster_aarch64_syntax_print_push_literal(machine, S8(")"))) return false;
            return buster_aarch64_syntax_print_push_children_reverse(machine, node.child_first, node.child_count);
        }
        u32 selected = 0;
        if (!buster_aarch64_syntax_print_choice(machine, index, &selected) || selected == BUSTER_AARCH64_SYNTAX_CHOICE_CANONICAL)
            return false;
        return buster_aarch64_syntax_print_branch(machine, node, selected);
    }
    BUSTER_UNUSED(index);
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_process_task(BusterAarch64SyntaxPrintMachine* machine,
                                                                    BusterAarch64SyntaxPrintTask task)
{
    if (task.kind == BUSTER_AARCH64_SYNTAX_PRINT_TASK_LITERAL) return buster_aarch64_syntax_print_emit(machine, task.literal);
    BusterAarch64SyntaxNode node = {0};
    return buster_aarch64_syntax_node(task.node_index, &node) &&
           buster_aarch64_syntax_print_process_node(machine, task.node_index, node);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_walk(BusterAarch64SyntaxRow row, BusterAarch64SyntaxPrintRequest request,
                                                            bool concrete, char8* output, u64 capacity, u64* length)
{
    if (concrete && ((request.capture_count && !request.captures) || (request.choice_count && !request.choices))) return false;
    BusterAarch64SyntaxPrintMachine machine = {.request = request, .concrete = concrete, .output = output, .capacity = capacity};
    if (!buster_aarch64_syntax_print_push_node(&machine, row.node_first)) return false;
    while (machine.task_count)
    {
        BusterAarch64SyntaxPrintTask task = machine.tasks[--machine.task_count];
        if (!buster_aarch64_syntax_print_process_task(&machine, task)) return false;
        /* The first walk is a measure-only pass (output == NULL), so a zero
           capacity there must not reject otherwise valid output.  Capacity is
           enforced during the write pass, and the measured size is checked
           before any bytes are emitted. */
        if (machine.output && machine.length > capacity) return false;
    }
    if (machine.anchor_count != (concrete ? request.capture_count : row.anchor_count)) return false;
    if (concrete && machine.choice_count != request.choice_count) return false;
    if (concrete && machine.anchor_count < row.anchor_min) return false;
    if (length) *length = machine.length;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_print_row_mode(u32 row_index, BusterAarch64SyntaxPrintRequest request,
                                                               BusterAarch64SyntaxOutput* output, bool concrete)
{
    BusterAarch64SyntaxRow row = {0};
    if (!output || !buster_aarch64_syntax_row(row_index, &row) || output->length > output->capacity ||
        (!output->pointer && output->capacity) || request.capture_count > BUSTER_AARCH64_SYNTAX_GENERATED_MAX_ANCHOR_OPERANDS ||
        request.choice_count > BUSTER_AARCH64_SYNTAX_GENERATED_MAX_CHOICE_COUNT)
        return false;
    u64 required = 0;
    if (!buster_aarch64_syntax_print_walk(row, request, concrete, 0, 0, &required) || required > output->capacity - output->length) return false;
    u64 original_length = output->length;
    u64 written = 0;
    char8* write_pointer = output->pointer ? output->pointer + original_length : 0;
    if (!buster_aarch64_syntax_print_walk(row, request, concrete, write_pointer,
                                          output->capacity - original_length, &written) || written != required) return false;
    output->length = original_length + written;
    return true;
}

BUSTER_F_DECL bool buster_aarch64_syntax_print_row(u32 row_index, BusterAarch64SyntaxPrintRequest request,
                                                    BusterAarch64SyntaxOutput* output)
{
    return buster_aarch64_syntax_print_row_mode(row_index, request, output, false);
}

BUSTER_F_DECL bool buster_aarch64_syntax_print_concrete_row(u32 row_index, BusterAarch64SyntaxPrintRequest request,
                                                             BusterAarch64SyntaxOutput* output)
{
    return buster_aarch64_syntax_print_row_mode(row_index, request, output, true);
}

BUSTER_GLOBAL_LOCAL u64 buster_aarch64_syntax_hash_append(u64 hash, String8 text)
{
    for (u64 index = 0; index < text.length; index += 1)
    {
        hash ^= (u8)text.pointer[index];
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

BUSTER_GLOBAL_LOCAL u64 buster_aarch64_syntax_source_hash(BusterAarch64SyntaxRow row)
{
    String8 kind = row.kind == BUSTER_AARCH64_SYNTAX_ROW_CANONICAL ? S8("canonical") : S8("alias");
    u64 hash = UINT64_C(0xcbf29ce484222325);
    hash = buster_aarch64_syntax_hash_append(hash, row.id);
    hash = buster_aarch64_syntax_hash_append(hash, (String8){.pointer = (char8*)"\0", .length = 1});
    hash = buster_aarch64_syntax_hash_append(hash, kind);
    hash = buster_aarch64_syntax_hash_append(hash, (String8){.pointer = (char8*)"\0", .length = 1});
    hash = buster_aarch64_syntax_hash_append(hash, row.encoding_name);
    hash = buster_aarch64_syntax_hash_append(hash, (String8){.pointer = (char8*)"\0", .length = 1});
    return buster_aarch64_syntax_hash_append(hash, row.assembly);
}

BUSTER_GLOBAL_LOCAL int buster_aarch64_syntax_string_compare(String8 left, String8 right)
{
    u64 common = BUSTER_MIN(left.length, right.length);
    for (u64 index = 0; index < common; index += 1)
    {
        u8 a = (u8)left.pointer[index];
        u8 b = (u8)right.pointer[index];
        if (a != b) return a < b ? -1 : 1;
    }
    return left.length == right.length ? 0 : (left.length < right.length ? -1 : 1);
}

BUSTER_GLOBAL_LOCAL bool buster_aarch64_syntax_row_lexical_mnemonic(BusterAarch64SyntaxRow row, String8* result)
{
    if (!result || (!row.assembly.pointer && row.assembly.length)) return false;
    u64 length = 0;
    while (length < row.assembly.length && row.assembly.pointer[length] != ' ' && row.assembly.pointer[length] != '\t') length += 1;
    if (!length) return false;
    String8 token = {.pointer = row.assembly.pointer, .length = length};
    for (u64 index = 0; index < token.length; index += 1)
    {
        if (token.pointer[index] == '{')
        {
            token.length = index;
            break;
        }
    }
    if (token.length >= 2 && token.pointer[0] == 'B' && token.pointer[1] == '.') token.length = 1;
    *result = token;
    return token.length != 0;
}

BUSTER_F_DECL bool buster_aarch64_syntax_validate(void)
{
    BusterAarch64SyntaxCounts counts = buster_aarch64_syntax_counts();
    if (counts.row_count != BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT ||
        counts.canonical_row_count != BUSTER_AARCH64_SYNTAX_GENERATED_CANONICAL_ROW_COUNT ||
        counts.alias_row_count != BUSTER_AARCH64_SYNTAX_GENERATED_ALIAS_ROW_COUNT ||
        counts.node_count != BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT ||
        counts.string_pool_bytes != BUSTER_AARCH64_SYNTAX_GENERATED_STRING_POOL_SIZE ||
        counts.mnemonic_range_count != BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_COUNT ||
        counts.mnemonic_candidate_count != BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT ||
        counts.optional_node_count != BUSTER_AARCH64_SYNTAX_GENERATED_OPTIONAL_NODE_COUNT ||
        counts.alt_node_count != BUSTER_AARCH64_SYNTAX_GENERATED_ALT_NODE_COUNT ||
        counts.mem_node_count != BUSTER_AARCH64_SYNTAX_GENERATED_MEM_NODE_COUNT ||
        counts.mem_writeback_count != BUSTER_AARCH64_SYNTAX_GENERATED_MEM_WRITEBACK_COUNT ||
        counts.list_node_count != BUSTER_AARCH64_SYNTAX_GENERATED_LIST_NODE_COUNT ||
        counts.lane_node_count != BUSTER_AARCH64_SYNTAX_GENERATED_LANE_NODE_COUNT ||
        counts.anchor_occurrence_count != BUSTER_AARCH64_SYNTAX_GENERATED_ANCHOR_OCCURRENCE_COUNT ||
        counts.anchor_alternative_count != BUSTER_AARCH64_SYNTAX_GENERATED_ANCHOR_ALTERNATIVE_COUNT ||
        counts.range_anchor_count != BUSTER_AARCH64_SYNTAX_GENERATED_RANGE_ANCHOR_COUNT ||
        counts.mnemonic_optional_suffix_count != BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_OPTIONAL_SUFFIX_COUNT ||
        counts.mnemonic_condition_count != BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CONDITION_COUNT ||
        counts.fixed_numeric_literal_count != BUSTER_AARCH64_SYNTAX_GENERATED_FIXED_NUMERIC_LITERAL_COUNT)
        return false;
    u32 expected_node_first = 0;
    for (u32 row_index = 0; row_index < counts.row_count; row_index += 1)
    {
        BusterAarch64SyntaxRow row = {0};
        if (!buster_aarch64_syntax_row(row_index, &row) || !buster_aarch64_syntax_node_range_valid(row.node_first, row.node_count)) return false;
        if (row.node_first != expected_node_first || row.kind >= BUSTER_AARCH64_SYNTAX_ROW_KIND_COUNT ||
            row.id.length == 0 || row.encoding_name.length == 0 || row.assembly.length == 0 ||
            buster_aarch64_syntax_source_hash(row) != row.source_hash || row.anchor_min > row.anchor_max ||
            row.anchor_max > row.anchor_count || row.node_count > BUSTER_AARCH64_SYNTAX_GENERATED_MAX_TOTAL_AST_NODES ||
            row.node_count > BUSTER_AARCH64_SYNTAX_GENERATED_MAX_WORK_ITEMS ||
            row.anchor_count > BUSTER_AARCH64_SYNTAX_GENERATED_MAX_ANCHOR_OPERANDS ||
            row.assembly.length > BUSTER_AARCH64_SYNTAX_GENERATED_MAX_ASSEMBLY_BYTES)
            return false;
        if (row_index)
        {
            BusterAarch64SyntaxRow previous = {0};
            if (!buster_aarch64_syntax_row(row_index - 1, &previous) || buster_aarch64_syntax_string_compare(previous.id, row.id) >= 0)
                return false;
        }
        String8 lexical_mnemonic = {0};
        if (!buster_aarch64_syntax_row_lexical_mnemonic(row, &lexical_mnemonic) ||
            !buster_aarch64_syntax_ascii_bytes_equal(lexical_mnemonic, row.mnemonic)) return false;
        u32 anchors = 0;
        u32 choices = 0;
        for (u32 validation_offset = 0; validation_offset < row.node_count; validation_offset += 1)
        {
            BusterAarch64SyntaxNode node = {0};
            if (!buster_aarch64_syntax_node(row.node_first + validation_offset, &node)) return false;
            if (node.kind == BUSTER_AARCH64_SYNTAX_ANCHOR) anchors += 1;
            if (node.kind == BUSTER_AARCH64_SYNTAX_OPTIONAL || node.kind == BUSTER_AARCH64_SYNTAX_ALT) choices += 1;
            if (!buster_aarch64_syntax_child_range_valid(node.child_first, node.child_count)) return false;
        }
        if (anchors != row.anchor_count || choices > BUSTER_AARCH64_SYNTAX_GENERATED_MAX_CHOICE_COUNT ||
            choices > BUSTER_AARCH64_SYNTAX_GENERATED_MAX_BACKTRACK_FRAMES ||
            anchors > BUSTER_AARCH64_SYNTAX_GENERATED_MAX_BACKTRACK_FRAMES - choices)
            return false;
        expected_node_first += row.node_count;
    }
    if (expected_node_first != counts.node_count) return false;

    u8 candidate_seen[BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT] = {0};
    u32 expected_candidate_first = 0;
    String8 previous_key = {0};
    for (u32 range_index = 0; range_index < counts.mnemonic_range_count; range_index += 1)
    {
        BusterAarch64SyntaxMnemonicRange range = {0};
        if (!buster_aarch64_syntax_mnemonic_range_at(range_index, &range) || range.key.length == 0 ||
            range.candidate_first != expected_candidate_first || range.candidate_count == 0 ||
            (range_index && buster_aarch64_syntax_string_compare(previous_key, range.key) >= 0)) return false;
        previous_key = range.key;
        for (u32 candidate_offset = 0; candidate_offset < range.candidate_count; candidate_offset += 1)
        {
            u32 row_index = 0;
            if (!buster_aarch64_syntax_mnemonic_candidate(range, candidate_offset, &row_index) ||
                row_index >= counts.row_count || candidate_seen[row_index]) return false;
            BusterAarch64SyntaxRow row = {0};
            if (!buster_aarch64_syntax_row(row_index, &row) ||
                !buster_aarch64_syntax_ascii_bytes_equal(row.mnemonic, range.key)) return false;
            candidate_seen[row_index] = 1;
        }
        expected_candidate_first += range.candidate_count;
    }
    if (expected_candidate_first != counts.mnemonic_candidate_count) return false;
    for (u32 row_index = 0; row_index < counts.row_count; row_index += 1)
    {
        if (!candidate_seen[row_index]) return false;
    }

    if (buster_aarch64_syntax_source_digest().length != 64 || buster_aarch64_syntax_input_digest().length != 64 || buster_aarch64_syntax_generic_shape_digest().length != 64 ||
        buster_aarch64_syntax_generic_row_digest().length != 64 || buster_aarch64_syntax_exact_shape_digest().length != 64 ||
        buster_aarch64_syntax_exact_row_digest().length != 64)
        return false;
    return true;
}
