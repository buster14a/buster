// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
struct S { int value; };
typedef struct S Record;
typedef _Atomic(int) Atomic;
void condition_if(Record s) { if (s) { } }
void condition_while(Record s) { while (s) { } }
void condition_do(Record s) { do { } while (s); }
void condition_for(Record s) { for (; s;) { } }
void condition_switch(Record s) { switch (s) { default: break; } }
void atomic_if(int x) { if ((_Atomic int)x) { } }
void atomic_while(int x) { while ((Atomic)x) { } }
void atomic_do(int x) { do { } while ((_Atomic int)x); }
void atomic_for(int x) { for (; (Atomic)x;) { } }
void atomic_switch(int x) { switch ((_Atomic int)x) { default: break; } }
void nested(Record s, int x)
{
    do { if (x) { while (s) { } } } while (x);
    while (x) { do { } while (s); }
    for (; x;) { if (s) { } }
    if (x) { for (; s;) { } }
}
