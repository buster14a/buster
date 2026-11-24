// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

__attribute__((noinline, patchable_function_entry(5, 0, "probe"))) int probe(int x)
{
    return x + 2;
}

int (*volatile callback)(int) = probe;

extern int getpid(void);
extern char** environ;

int (*volatile process_id)(void) = getpid;
char*** volatile environment = &environ;

__attribute__((noinline, patchable_function_entry(5, 0, "getpid"))) int process_exists(void)
{
    return getpid() > 0 && process_id() > 0;
}

__attribute__((noinline, patchable_function_entry(5, 0, "environ"))) int has_environment(void)
{
    return *environment != 0;
}

__attribute__((noinline, patchable_function_entry(7, 0, "later"))) int earlier(int x)
{
    return callback(x) + 5;
}

__attribute__((noinline)) int later(int x)
{
    return earlier(x) + 7;
}

int main(int argc, char** argv)
{
    return later(argc) != 15 || !process_exists() || !has_environment();
}
