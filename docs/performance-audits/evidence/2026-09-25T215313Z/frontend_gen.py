#!/usr/bin/env python3
"""gen3.py FAMILY T Q OUT.c -- T = filler type rows that are not aggregate
definitions (distinct array types), Q = queries/definitions."""
import sys
def filler(t):
    return [f"extern int filler{i}[{i + 1}];" for i in range(t)]
def defs(t, q):  # Q aggregate definitions after T filler rows
    return filler(t) + [f"struct D{i} {{ int a{i}; }};" for i in range(q)] + ["int f(void) { return 0; }"]
def anon(t, q):
    s = filler(t) + ["struct Node { int key; union { long payload; double real; }; };", "long f(struct Node *p) { long x = 0;"]
    return s + ["  x += p->payload;" for _ in range(q)] + ["  return x; }"]
def off(t, q):
    s = ["#include <stddef.h>", "struct Node { int key; struct Node *next; long payload; };"] + filler(t)
    return s + ["_Static_assert(offsetof(struct Node, payload) == 16, \"layout\");" for _ in range(q)] + ["int f(void) { return 0; }"]
F = {"defs": defs, "anon": anon, "off": off}
fam, t, q, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
open(out, "w").write("\n".join(F[fam](t, q)) + "\n")
