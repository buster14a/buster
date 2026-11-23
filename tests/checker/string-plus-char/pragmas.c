// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstring-plus-int"
const char* character_only(void) { return "text" + 'x'; }
#pragma clang diagnostic ignored "-Wstring-plus-char"
const char* both_ignored(void) { return "text" + 'x'; }
#pragma clang diagnostic warning "-Wstring-plus-int"
const char* integer_only(void) { return "text" + 'x'; }
#pragma clang diagnostic pop
const char* both_restored(void) { return "text" + 'x'; }
