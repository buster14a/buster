#pragma once

#include <buster/lib/compiler/codegen/machine.h>

#if BUSTER_INCLUDE_TESTS
// Test-only bounded entry to the production switch-constant emitter. The
// reference selects the original metadata path without mutating shared plans.
// The test runner serially prewarms the exact map before publishing workers.
BUSTER_F_DECL bool machine_x64_test_movabs_prepared(void);
BUSTER_F_DECL MachineEncodeResult machine_x64_test_emit_movabs(u8* bytes, u32 capacity, u32 start, u32 reg, u64 value, bool reference);
#endif
