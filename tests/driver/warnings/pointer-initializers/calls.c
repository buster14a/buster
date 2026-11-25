// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
typedef const char ConstArray[3];
const char fixed[3];
volatile char changing[3];
ConstArray aliased;
void take(char *);
void use(void)
{
    take(fixed);
    take(changing);
    take((aliased));
    take(&fixed[0]);
}
