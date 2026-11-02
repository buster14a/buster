// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// BUSTER_BAKED puts a variable in the executable's buster_baked section (#590): the state the backend's initializers
// write (x86_encoder_initialize's table for x86_encode, x86_semantics_initialize's, tier0_initialize's and
// unwind_initialize's), which build.sh has the linked executable write into its own file (bake-tables, baked.c), so
// that a compile starts with it built rather than building it. The section is writable data with file bytes: a process
// reads its pages from the page cache, shared with every other process, as they are touched, and an executable that
// isn't baked (a PIE, or one linked without build.sh) builds them as before. BUSTER_BAKED is for the state main.c's
// units share (x86_64_encoder.c's BUSTER_X86_SHARED), and BUSTER_BAKED_UNIT for a module's static state, which is
// baked only in the checker's unit, the C driver's: another unit's copy (the backend's tests') would only add its bytes
// to the file. A unit of its own (a benchmark's) keeps its copies in .bss.
#ifndef BUSTER_BAKED_H
#define BUSTER_BAKED_H
#if defined(BUSTER_UNIT)
#define BUSTER_BAKED __attribute__((section("buster_baked")))
#else
#define BUSTER_BAKED
#endif
#if defined(BUSTER_UNIT) && BUSTER_UNIT == BUSTER_UNIT_CHECKER
#define BUSTER_BAKED_UNIT BUSTER_BAKED
#else
#define BUSTER_BAKED_UNIT
#endif
#endif
