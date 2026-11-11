// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#include "fixture_test.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

int codegen_shared_value = 7;

typedef struct CodegenFixtureThread CodegenFixtureThread;
struct CodegenFixtureThread
{
    int* alpha_tls;
    int* beta_tls;
};

static void* codegen_thread(void* argument)
{
    const CodegenFixtureThread* previous = argument;
    const CodegenFixtureResult alpha = codegen_alpha(1);
    const CodegenFixtureResult beta = codegen_beta(1);
    assert(alpha.value == 124 && beta.value == 122);
    assert(alpha.data != beta.data && alpha.tls != beta.tls && alpha.function != beta.function);
    assert(alpha.tls != previous->alpha_tls && beta.tls != previous->beta_tls);
    return 0;
}

int main(void)
{
    assert(codegen_optional == 0);
    const CodegenFixtureResult alpha = codegen_alpha(1);
    const CodegenFixtureResult beta = codegen_beta(2);
    assert(alpha.value == 120 && beta.value == 123);
    assert(alpha.data != beta.data && alpha.tls != beta.tls && alpha.function != beta.function);
    assert(codegen_alpha(3).value == 129);
    const CodegenFixtureThread previous = {
        .alpha_tls = alpha.tls,
        .beta_tls = beta.tls,
    };
    pthread_t thread;
    assert(pthread_create(&thread, 0, codegen_thread, (void*)&previous) == 0);
    assert(pthread_join(thread, 0) == 0);
    assert(codegen_alpha(1).value == 133);
    assert(codegen_beta(1).value == 127);
    puts("codegen bindings: independent private function/data/TLS and shared strong/weak externals passed");
    return 0;
}
