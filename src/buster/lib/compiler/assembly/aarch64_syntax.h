#pragma once

#include <buster/lib/base.h>

/*
 * AArch64 display-template syntax metadata.
 *
 * The generated table is deliberately pointer-free.  Public views contain
 * String8 spans into the immutable generated pool, while every index and
 * range is checked before it is dereferenced.  This module describes syntax
 * shape only; it does not infer encoder fields or register/immediate
 * transforms from an anchor spelling.
 */

#define BUSTER_AARCH64_SYNTAX_SCHEMA_VERSION 1u
#define BUSTER_AARCH64_SYNTAX_INVALID_INDEX UINT32_MAX

typedef enum BusterAarch64SyntaxNodeKind
{
    BUSTER_AARCH64_SYNTAX_MNEMONIC,
    BUSTER_AARCH64_SYNTAX_SEQ,
    BUSTER_AARCH64_SYNTAX_LIT,
    BUSTER_AARCH64_SYNTAX_ANCHOR,
    BUSTER_AARCH64_SYNTAX_OPTIONAL,
    BUSTER_AARCH64_SYNTAX_ALT,
    BUSTER_AARCH64_SYNTAX_MEM,
    BUSTER_AARCH64_SYNTAX_LIST,
    BUSTER_AARCH64_SYNTAX_LANE,
    BUSTER_AARCH64_SYNTAX_NODE_KIND_COUNT,
} BusterAarch64SyntaxNodeKind;

enum
{
    BUSTER_AARCH64_SYNTAX_ANCHOR_ALTERNATIVE = 1u << 0,
    BUSTER_AARCH64_SYNTAX_ANCHOR_RANGE = 1u << 1,

    BUSTER_AARCH64_SYNTAX_LIT_FIXED_NUMERIC = 1u << 0,
    BUSTER_AARCH64_SYNTAX_LIT_SHIFT = 1u << 1,
    BUSTER_AARCH64_SYNTAX_LIT_EXTEND = 1u << 2,
    BUSTER_AARCH64_SYNTAX_LIT_DELIMITER = 1u << 3,

    BUSTER_AARCH64_SYNTAX_MNEMONIC_CONDITION = 1u << 0,
    BUSTER_AARCH64_SYNTAX_MNEMONIC_OPTIONAL_SUFFIX = 1u << 1,

    BUSTER_AARCH64_SYNTAX_ALT_IMPLICIT_DELIMITER = 1u << 0,
    BUSTER_AARCH64_SYNTAX_MEM_WRITEBACK = 1u << 0,
};

typedef enum BusterAarch64SyntaxRowKind
{
    BUSTER_AARCH64_SYNTAX_ROW_CANONICAL,
    BUSTER_AARCH64_SYNTAX_ROW_ALIAS,
    BUSTER_AARCH64_SYNTAX_ROW_KIND_COUNT,
} BusterAarch64SyntaxRowKind;

typedef struct BusterAarch64SyntaxGeneratedNode BusterAarch64SyntaxGeneratedNode;
struct BusterAarch64SyntaxGeneratedNode
{
    u32 child_first;
    u32 child_count;
    u32 text_offset;
    u32 text_length;
    u16 kind_flags;
    u16 numeric_count;
    u16 reserved;
};

typedef struct BusterAarch64SyntaxGeneratedRow BusterAarch64SyntaxGeneratedRow;
struct BusterAarch64SyntaxGeneratedRow
{
    u32 node_first;
    u32 node_count;
    u32 id_offset;
    u32 id_length;
    u32 assembly_offset;
    u32 assembly_length;
    u32 mnemonic_offset;
    u32 mnemonic_length;
    u32 encoding_offset;
    u32 encoding_length;
    u32 anchor_count;
    u32 anchor_min;
    u32 anchor_max;
    u32 row_kind;
    u64 source_hash;
};

typedef struct BusterAarch64SyntaxGeneratedMnemonicRange BusterAarch64SyntaxGeneratedMnemonicRange;
struct BusterAarch64SyntaxGeneratedMnemonicRange
{
    u32 key_offset;
    u32 key_length;
    u32 candidate_first;
    u32 candidate_count;
};

typedef struct BusterAarch64SyntaxNode BusterAarch64SyntaxNode;
struct BusterAarch64SyntaxNode
{
    BusterAarch64SyntaxNodeKind kind;
    u8 flags;
    u16 reserved;
    u32 index;
    u32 child_first;
    u32 child_count;
    u16 numeric_count;
    String8 text;
};

typedef struct BusterAarch64SyntaxRow BusterAarch64SyntaxRow;
struct BusterAarch64SyntaxRow
{
    u32 index;
    u32 node_first;
    u32 node_count;
    u32 anchor_count;
    u32 anchor_min;
    u32 anchor_max;
    BusterAarch64SyntaxRowKind kind;
    u8 reserved[3];
    u64 source_hash;
    String8 id;
    String8 assembly;
    String8 mnemonic;
    String8 encoding_name;
};

typedef struct BusterAarch64SyntaxMnemonicRange BusterAarch64SyntaxMnemonicRange;
struct BusterAarch64SyntaxMnemonicRange
{
    String8 key;
    u32 candidate_first;
    u32 candidate_count;
};

typedef struct BusterAarch64SyntaxCounts BusterAarch64SyntaxCounts;
struct BusterAarch64SyntaxCounts
{
    u32 row_count;
    u32 canonical_row_count;
    u32 alias_row_count;
    u32 node_count;
    u32 string_pool_bytes;
    u32 mnemonic_range_count;
    u32 mnemonic_candidate_count;
    u32 optional_node_count;
    u32 alt_node_count;
    u32 mem_node_count;
    u32 mem_writeback_count;
    u32 list_node_count;
    u32 lane_node_count;
    u32 anchor_occurrence_count;
    u32 anchor_alternative_count;
    u32 range_anchor_count;
    u32 mnemonic_optional_suffix_count;
    u32 mnemonic_condition_count;
    u32 fixed_numeric_literal_count;
};

typedef struct BusterAarch64SyntaxAnchor BusterAarch64SyntaxAnchor;
struct BusterAarch64SyntaxAnchor
{
    u32 node_index;
    u32 occurrence;
    u8 flags;
    u8 reserved[3];
    String8 spelling;
};

typedef bool BusterAarch64SyntaxMatchAnchorCallback(void* user, BusterAarch64SyntaxAnchor anchor, String8 input,
                                                     u64* cursor);
typedef bool BusterAarch64SyntaxPrintAnchorCallback(void* user, BusterAarch64SyntaxAnchor anchor, String8* spelling);
typedef u64 BusterAarch64SyntaxCheckpointCallback(void* user);
typedef void BusterAarch64SyntaxRestoreCallback(void* user, u64 token);
typedef bool BusterAarch64SyntaxSelectAlternativeCallback(void* user, BusterAarch64SyntaxNode node, u32 branch_count,
                                                            u32* branch_index);
typedef bool BusterAarch64SyntaxSelectOptionalCallback(void* user, BusterAarch64SyntaxNode node, bool* present);

typedef struct BusterAarch64SyntaxCallbacks BusterAarch64SyntaxCallbacks;
struct BusterAarch64SyntaxCallbacks
{
    BusterAarch64SyntaxMatchAnchorCallback* match_anchor;
    BusterAarch64SyntaxPrintAnchorCallback* print_anchor;
    void* user;
    BusterAarch64SyntaxCheckpointCallback* checkpoint;
    BusterAarch64SyntaxRestoreCallback* restore;
    BusterAarch64SyntaxSelectAlternativeCallback* select_alternative;
    BusterAarch64SyntaxSelectOptionalCallback* select_optional;
};

typedef struct BusterAarch64SyntaxOutput BusterAarch64SyntaxOutput;
struct BusterAarch64SyntaxOutput
{
    char8* pointer;
    u64 length;
    u64 capacity;
};

typedef struct BusterAarch64SyntaxStats BusterAarch64SyntaxStats;
struct BusterAarch64SyntaxStats
{
    u32 generic_shape_count;
    u32 exact_shape_count;
    u32 max_total_ast_nodes;
    u32 max_non_lit_non_seq_nodes;
    u32 max_optional_depth;
    u32 max_delimiter_nesting;
    u32 max_top_level_comma_groups;
    u32 max_anchor_operands;
    u64 input_digest_hi;
    u64 input_digest_lo;
};

BUSTER_F_DECL u32 buster_aarch64_syntax_schema_version(void);
BUSTER_F_DECL BusterAarch64SyntaxCounts buster_aarch64_syntax_counts(void);
BUSTER_F_DECL BusterAarch64SyntaxStats buster_aarch64_syntax_stats(void);
BUSTER_F_DECL String8 buster_aarch64_syntax_source_digest(void);
BUSTER_F_DECL String8 buster_aarch64_syntax_input_digest(void);
BUSTER_F_DECL String8 buster_aarch64_syntax_generic_shape_digest(void);
BUSTER_F_DECL String8 buster_aarch64_syntax_generic_row_digest(void);
BUSTER_F_DECL String8 buster_aarch64_syntax_exact_shape_digest(void);
BUSTER_F_DECL String8 buster_aarch64_syntax_exact_row_digest(void);
BUSTER_F_DECL bool buster_aarch64_syntax_string(u32 offset, u32 length, String8* result);
BUSTER_F_DECL bool buster_aarch64_syntax_row(u32 index, BusterAarch64SyntaxRow* result);
BUSTER_F_DECL bool buster_aarch64_syntax_node(u32 index, BusterAarch64SyntaxNode* result);
BUSTER_F_DECL bool buster_aarch64_syntax_node_child(BusterAarch64SyntaxNode node, u32 child_index, u32* result);
BUSTER_F_DECL bool buster_aarch64_syntax_node_range_valid(u32 first, u32 count);
BUSTER_F_DECL bool buster_aarch64_syntax_mnemonic_range_at(u32 index, BusterAarch64SyntaxMnemonicRange* result);
BUSTER_F_DECL bool buster_aarch64_syntax_mnemonic_lookup(String8 mnemonic, BusterAarch64SyntaxMnemonicRange* result);
BUSTER_F_DECL bool buster_aarch64_syntax_mnemonic_candidate(BusterAarch64SyntaxMnemonicRange range, u32 index, u32* row_index);
BUSTER_F_DECL bool buster_aarch64_syntax_validate(void);

/* Transactional syntax matcher and display-template printer.  Both require a complete row and
 * reject trailing input/output failures; branch attempts restore cursor,
 * anchor occurrence, and logical output length before trying the next branch.
 * print_row emits the source display template, including optional braces and
 * ALT separators.  print_concrete_row uses the selector callbacks below to
 * omit display-only delimiters and emit one legal concrete spelling.
 */
BUSTER_F_DECL bool buster_aarch64_syntax_match_row(u32 row_index, String8 input,
                                                    BusterAarch64SyntaxCallbacks callbacks);
BUSTER_F_DECL bool buster_aarch64_syntax_print_row(u32 row_index, BusterAarch64SyntaxCallbacks callbacks,
                                                    BusterAarch64SyntaxOutput* output);
BUSTER_F_DECL bool buster_aarch64_syntax_print_concrete_row(u32 row_index, BusterAarch64SyntaxCallbacks callbacks,
                                                             BusterAarch64SyntaxOutput* output);
