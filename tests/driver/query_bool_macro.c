// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Operator and literal expansion notes differ, and forwarded arguments keep their original source location.
#define SHIFT (1 << 2)
#define CONDITIONAL (SHIFT ? 3 : 4)
#define SELECT(x) ((x) ? 3 : 4)
#define FORWARD(x) SELECT(x)
#define HALF 0.5
#define FLOAT_CONDITIONAL (HALF ? 3 : 4)
int macro_expr(void) { return sizeof(SHIFT ? 3 : 4); }
int macro_all(void) { return sizeof(CONDITIONAL); }
int macro_arg(void) { return sizeof(SELECT(1 << 2)); }
int macro_forward(void) { return sizeof(FORWARD(1 << 2)); }
int macro_float(void) { return sizeof(HALF ? 3 : 4); }
int macro_float_all(void) { return sizeof(FLOAT_CONDITIONAL); }
int macro_float_arg(void) { return sizeof(SELECT(0.5)); }
int macro_float_forward(void) { return sizeof(FORWARD(0.5)); }
typedef int MacroBound[SELECT(1 << 2)];
typedef __typeof__(SELECT(0.5)) MacroQuery;
