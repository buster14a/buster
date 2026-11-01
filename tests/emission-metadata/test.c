// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone wrapper for the emission metadata protocol tests. The normal build compiles
// src/buster/compiler/emission_metadata_test.c as a translation unit of the compiler and runs it through
// ./build/compiler test-emission-metadata; this wrapper gives the isolated baseline, AVX-512F and sanitizer commands in
// docs/emission-metadata.md the same tests without the compiler. It is original C data.

#define BUSTER_EMISSION_METADATA_TEST_STANDALONE 1
#include "../../src/buster/compiler/emission_metadata_test.c"
