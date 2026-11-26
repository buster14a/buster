// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
typedef char *Pointer;
typedef const char *ConstPointer;
typedef const char ConstArray[2];
ConstPointer cp;
ConstArray fixed;
struct S { Pointer member; };
struct T { Pointer array[2]; struct S nested; };
void use(void)
{
    Pointer scalar = { cp };
    Pointer array[1] = { cp };
    struct S object = { cp };
    Pointer from_array = { fixed };
    struct T aggregate = { { cp, cp }, { cp } };
    (void)scalar;
    (void)array;
    (void)object;
    (void)from_array;
    (void)aggregate;
}
