// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#ifndef BUSTER_CODEGEN_FIXTURE_TEST_INCLUDED
#define BUSTER_CODEGEN_FIXTURE_TEST_INCLUDED

typedef struct CodegenFixtureResult CodegenFixtureResult;
struct CodegenFixtureResult
{
    int value;
    int* data;
    int* tls;
    int (*function)(int);
};

extern int codegen_shared_value;
extern int codegen_optional(void) __attribute__((weak));
__attribute__((visibility("hidden"))) CodegenFixtureResult codegen_alpha(int step);
__attribute__((visibility("hidden"))) CodegenFixtureResult codegen_beta(int step);

#endif
