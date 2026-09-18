#pragma once

#include <buster/lib/compiler/wasm/wasm.h>

#if BUSTER_INCLUDE_TESTS
// Private arithmetic seam for overflow cases that cannot be materialized as a
// bounded C fixture without attempting impossible arena reservations.
typedef struct Wasm64CheckedArithmeticProbe Wasm64CheckedArithmeticProbe;
struct Wasm64CheckedArithmeticProbe
{
    bool alignment_overflow_rejected;
    bool frame_addition_overflow_rejected;
    bool maximum_byte_count_rounded_up;
    bool excessive_page_count_rejected;
};

BUSTER_F_DECL Wasm64CheckedArithmeticProbe wasm64_test_checked_arithmetic(void);
#endif
