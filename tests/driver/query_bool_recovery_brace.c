// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Source conversion receipts survive the rejected view before a later missing brace.
int f(void) { int x = undeclared; return sizeof((1 << 2) ? 3 : 4);
