#pragma once
#include <buster/lib/compiler/jit/jit.h>

#if BUSTER_INCLUDE_TESTS
// Deterministic test seam: the number of host-binding entries visited by name
// or kind comparison, or by an index probe, on the calling thread since the
// last reset. It counts every visit made by jit_link_object, including index
// construction, so tests can pin that resolution work does not scale with the
// relocation count.
BUSTER_F_DECL void jit_test_binding_visits_reset(void);
BUSTER_F_DECL u64 jit_test_binding_visits(void);
#endif
