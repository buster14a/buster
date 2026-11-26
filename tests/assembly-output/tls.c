// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#include <pthread.h>

__thread int shared_tls = 11;
static __thread int local_tls = 31;
__thread int zero_tls;
// A TLS definition in another module exercises initial-exec without PIC and general-dynamic with PIC.
extern __thread int library_tls __asm__("errno");

static void* worker(void* unused)
{
    (void)unused;
    const int initial = shared_tls + local_tls + zero_tls;
    shared_tls += 7;
    local_tls += 9;
    zero_tls = 5;
    library_tls = 7;
    return initial == 42 && shared_tls + local_tls + zero_tls + library_tls == 70 ? 0 : (void*)1;
}

int main(void)
{
    pthread_t thread;
    void* result = (void*)1;
    shared_tls = 100;
    local_tls = 200;
    zero_tls = 300;
    library_tls = 301;
    int valid = pthread_create(&thread, 0, worker, 0) == 0;
    if (valid)
    {
        valid = pthread_join(thread, &result) == 0 && result == 0;
    }
    return valid && shared_tls == 100 && local_tls == 200 && zero_tls == 300 && library_tls == 301 ? 0 : 1;
}
