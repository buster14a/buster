#pragma once

#include <buster/lib/compiler/assembly/assembly_unit.h>

#if BUSTER_INCLUDE_TESTS
// Narrow test seam for the integer parser shared by directive expressions and
// local numeric labels. Failed parses leave the caller's output untouched.
BUSTER_F_DECL bool assembly_unit_test_parse_number(String8 text, u64* value);
#endif
