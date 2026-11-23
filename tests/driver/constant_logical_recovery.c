// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

enum LogicalRecovery
{
    logical_recovery_file = 1 && 4,
};

int logical_before_recovery(int x)
{
    return x || 8;
}

int logical_missing_initializer = absent_at_file_scope;

int logical_block_recovery(int x)
{
    int value = x && 16;
    int missing = logical_unknown_local;
    return value;
}

int logical_independent_error = logical_unknown_name;

int logical_after_recovery(int x)
{
    return x || 32;
}
