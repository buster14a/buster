// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 annotations carry sensitivity independently from their argument type.
void private_format(const char *) __attribute__((format_matches(os_log, 1, "%{private}s")));
void plain_format(const char *) __attribute__((format_matches(printf, 1, "%s")));
void sensitivity(void)
{
    private_format("%{private}s");
    private_format("%{public}s");
    private_format("%s");
    private_format("%{sensitive}s");
    private_format("%{public}d");
    plain_format("%{private}s");
    plain_format("%{sensitive}s");
}
