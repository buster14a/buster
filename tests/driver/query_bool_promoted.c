// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Source promotion remains active under a later command-line -Wno-error; ignored categories stay separate.
#pragma clang diagnostic push
#pragma clang diagnostic error "-Wtautological-constant-compare"
typedef int Promoted[(1 << 2) ? 3 : 4];
#pragma clang diagnostic ignored "-Wliteral-conversion"
typedef __typeof__(0.5 ? 3 : 4) QuietFloat;
#pragma clang diagnostic pop
#pragma clang diagnostic push
#pragma clang diagnostic error "-Wliteral-conversion"
typedef __typeof__(0.5 ? 3 : 4) PromotedFloat;
#pragma clang diagnostic ignored "-Wtautological-constant-compare"
typedef int Quiet[(1 << 2) ? 3 : 4];
#pragma clang diagnostic pop
int restored_again(void) { return sizeof((1 << 2) ? 3 : 4); }
