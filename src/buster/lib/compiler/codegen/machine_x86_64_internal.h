#pragma once

#include <buster/lib/compiler/codegen/machine.h>

#if BUSTER_INCLUDE_TESTS
// Test-only bounded entry to the production switch-constant emitter. The
// reference selects the original metadata path without mutating shared plans.
// The test runner serially prewarms the exact map before publishing workers.
BUSTER_F_DECL bool machine_x64_test_movabs_prepared(void);
BUSTER_F_DECL MachineEncodeResult machine_x64_test_emit_movabs(u8* bytes, u32 capacity, u32 start, u32 reg, u64 value, bool reference);
// Read-only audit of the production fixed-template consumer, including
// real published patch fields, exact tails, output guards and refusals.
typedef struct MachineX64FixedTemplateAudit MachineX64FixedTemplateAudit;
struct MachineX64FixedTemplateAudit
{
    u32 rows;
    u32 byte_patch_rows;
    u32 dword_patch_rows;
    u32 cases;
    u32 failures;
    bool valid;
};
BUSTER_F_DECL MachineX64FixedTemplateAudit machine_x64_test_fixed_template_emission(void);
// Frame spill/reload chunk emitter; the reference is the metadata exact form.
BUSTER_F_DECL bool machine_x64_test_frame_chunk_prepared(void);
BUSTER_F_DECL MachineEncodeResult machine_x64_test_emit_frame_chunk(u8* bytes, u32 capacity, u32 start, u32 frame_base_offset, bool load,
                                                                    u32 reg, u32 offset, u32 chunk, bool reference);
// Test-only audit of every published dense row against the existing generic
// recipe projection and metadata authority, without mutating shared caches.
typedef struct MachineX64GprPreparationAudit MachineX64GprPreparationAudit;
struct MachineX64GprPreparationAudit
{
    u32 tables;
    u32 zero_register_tables;
    u32 one_register_tables;
    u32 two_register_tables;
    u32 rows;
    u32 distinct_rows;
    u32 replicated_rows;
    u32 cases;
    u32 failures;
    bool valid;
};
BUSTER_F_DECL MachineX64GprPreparationAudit machine_x64_test_gpr_preparation(void);
// Read-only publication audit and mutation seam for the same production
// condition/form join. A valid neighboring key must still fail its condition.
typedef struct MachineX64ConditionBindingAudit MachineX64ConditionBindingAudit;
struct MachineX64ConditionBindingAudit
{
    u32 checked_bindings;
    u32 mismatched_bindings;
};
BUSTER_F_DECL MachineX64ConditionBindingAudit machine_x64_test_condition_binding_audit(void);
BUSTER_F_DECL bool machine_x64_test_condition_form_agrees(u32 family, u32 condition, u32 form_id, u64 stable_hash);
#endif
