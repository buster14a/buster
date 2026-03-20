#pragma once

#include <buster/base.h>
#include <buster/compiler/ir/ir.h>

ENUM_T(X86SelectorLoweringId, u8,
    X86_SELECTOR_LOWERING_NONE,
    X86_SELECTOR_LOWERING_XOR_ZERO_GPR32,
    X86_SELECTOR_LOWERING_MOV_IMMEDIATE_GPR,
    X86_SELECTOR_LOWERING_MOV_REGISTER_GPR,
    X86_SELECTOR_LOWERING_ADD_REGISTER_GPR,
    X86_SELECTOR_LOWERING_ADD_IMMEDIATE_GPR,
    X86_SELECTOR_LOWERING_COUNT,
);

ENUM_T(X86SelectorBucketId, u8,
    X86_SELECTOR_BUCKET_RETURN_CONSTANT_INTEGER,
    X86_SELECTOR_BUCKET_COPY_INTEGER,
    X86_SELECTOR_BUCKET_ADD_INTEGER,
    X86_SELECTOR_BUCKET_XOR_INTEGER,
    X86_SELECTOR_BUCKET_COUNT,
);

ENUM(X86SelectorSourceKind,
    X86_SELECTOR_SOURCE_KIND_REGISTER,
    X86_SELECTOR_SOURCE_KIND_IMMEDIATE,
    X86_SELECTOR_SOURCE_KIND_MEMORY,
);

ENUM(X86SelectorRuleFlags,
    X86_SELECTOR_RULE_FLAG_NONE = 0,
    X86_SELECTOR_RULE_FLAG_COMMUTATIVE = 1 << 0,
    X86_SELECTOR_RULE_FLAG_MEMORY_SOURCE = 1 << 1,
    X86_SELECTOR_RULE_FLAG_IMMEDIATE_SOURCE = 1 << 2,
);

STRUCT(X86SelectorRule)
{
    u32 form_index;
    u32 width_bits;
    u32 flags;
    u32 cost;
    u8 operand_count;
    X86SelectorLoweringId lowering_id;
    IrTypeId type_id;
    u8 reserved[1];
};

STRUCT(X86SelectorRuleCost)
{
    u32 code_size_cost;
    u32 latency_cost;
    u32 throughput_cost;
    u32 total_cost;
};

STRUCT(X86SelectorBucket)
{
    u32 rule_start;
    u32 rule_count;
    X86SelectorBucketId id;
    IrTypeId type_id;
    u8 reserved[2];
};

STRUCT(X86SelectorSelection)
{
    u32 form_index;
    u32 immediate_width_bits;
    bool is_valid;
    X86SelectorLoweringId lowering_id;
    u8 reserved[2];
};

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_DECL UnitTestResult instruction_selection_tests(UnitTestArguments* arguments);
#endif
