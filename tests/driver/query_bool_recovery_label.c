// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// The query warning precedes the undefined-label error published at the function's completion.
int f(void) { int x = 1; goto missing; return sizeof((1 << 2) ? 3 : 4); }
