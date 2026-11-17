// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

const char* literal_right(void) { return "text" + 'x'; }
const char* literal_left(void) { return 'x' + "text"; }
const char* literal_parenthesized(void) { return ("text") + 'x'; }
const char* character_parenthesized(void) { return "text" + ('x'); }
const char* character_cast_int(void) { return "text" + (int)'x'; }
const char* character_cast_char(void) { return "text" + (char)'x'; }
const char* character_promoted(void) { return "text" + +'x'; }
const char* character_variable(char c) { return "text" + c; }
const char* literal_concatenated(void) { return "te" "xt" + 'x'; }
const char* literal_subtracted(void) { return "text" - 'x'; }
const int* wide_pointer(void) { return L"text" + L'x'; }

#define APPEND_CHARACTER(p) (p + 'x')
const char* character_macro(const char* p) { return APPEND_CHARACTER(p); }
