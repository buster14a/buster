// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#include "fixture_test.h"

// The other translation unit deliberately uses every private name below too. Partitioning must keep the two
// copies independent while retaining the hidden public entry and both strong and weak unresolved references.
static int private_data = 10;
static _Thread_local int private_tls = 100;

static __attribute__((noinline)) int private_function(int value)
{
    return value + codegen_shared_value + (codegen_optional != 0 ? codegen_optional() : 0);
}

__attribute__((visibility("hidden"))) CodegenFixtureResult codegen_alpha(int step)
{
    private_data += step;
    private_tls += 2 * step;
    return (CodegenFixtureResult){
        .value = private_function(private_data + private_tls),
        .data = &private_data,
        .tls = &private_tls,
        .function = private_function,
    };
}
