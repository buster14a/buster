// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Source policy follows the condition and the expansion point, independently of its query's completion.
int before(void) { return sizeof((1 << 2) ? 3 : 4); }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-constant-compare"
int ignored(void) { return sizeof((1 << 2) ? 3 : 4); }
#define QUIET_DEFINED (1 << 2)
#pragma clang diagnostic pop
int restored(void) { return sizeof((1 << 2) ? 3 : 4); }
int macro_outside(void) { return sizeof(QUIET_DEFINED ? 3 : 4); }
#define LOUD_DEFINED (1 << 2)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-constant-compare"
int macro_inside(void) { return sizeof(LOUD_DEFINED ? 3 : 4); }
#pragma clang diagnostic pop
int prior_operand(void) { return sizeof((1 << 2)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-constant-compare"
? 3 : 4); }
int ignored_operand(void) { return sizeof((1 << 2)
#pragma clang diagnostic pop
? 3 : 4); }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wliteral-conversion"
typedef __typeof__(0.5 ? 3 : 4) QuietFloat;
#pragma clang diagnostic pop
typedef __typeof__(0.5 ? 3 : 4) LoudFloat;
