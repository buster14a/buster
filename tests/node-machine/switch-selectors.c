// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

enum First { A, B, C };
enum Second { X, Y, Z };
struct S { enum Second kind; };
enum First get(void);

int selectors(enum First e, const enum First* array, struct S* s, int i)
{
    switch (e) { case A: break; }
    switch ((e)) { case B: break; }
    switch (s->kind) { case X: break; }
    switch (array[i]) { case C: break; }
    switch (get()) { case A: break; }
    switch ((enum Second)i) { case Y: break; }
    switch ((int)e) { case 2: break; }
    switch (+e) { case 2: break; }
    switch (i) { case 2: break; }
    return i;
}
