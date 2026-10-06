#pragma once

#include <buster/lib/compiler/codegen/machine.h>

#if BUSTER_INCLUDE_TESTS
// Test-only bounded entry to the production switch-constant emitter. The
// reference selects the original metadata path without mutating shared plans.
// The test runner serially prewarms the exact map before publishing workers.
BUSTER_F_DECL bool machine_x64_test_movabs_prepared(void);
BUSTER_F_DECL MachineEncodeResult machine_x64_test_emit_movabs(u8* bytes, u32 capacity, u32 start, u32 reg, u64 value, bool reference);
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
#endif
