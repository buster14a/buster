// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Installed Linux/x86-64 runtime locations, in the same order for the linker and the floors' input discovery.
#ifndef BUSTER_RUNTIME_PATHS_H
#define BUSTER_RUNTIME_PATHS_H

// glibc's directories, in the order GCC's driver searches them on these systems.
static const char* const runtime_library_directories[] = {
    "/usr/lib/x86_64-linux-gnu",
    "/lib/x86_64-linux-gnu",
    "/usr/lib64",
    "/lib64",
    "/usr/lib",
    "/lib",
};

#endif
