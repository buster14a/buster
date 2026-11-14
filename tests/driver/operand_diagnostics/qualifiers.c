// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
struct S { int value; };
typedef struct S Record;
typedef const struct S ConstRecord;
void qualifiers(const Record r, ConstRecord cr, int x)
{
    (void)-r;
    (void)-cr;
    if (r) { }
    if (cr) { }
    (void)(r ? 1 : 2);
    (void)(cr ? 1 : 2);
    (void)(x ? r : 2);
    (void)(x ? 2 : cr);
    (void)*r;
    (void)*cr;
    (void)(r < x);
    (void)(x > cr);
    switch (r) { default: break; }
    switch (cr) { default: break; }
}
