// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

const char* zero_value(const char* p) { return p + '\0'; }
const char* negative_byte(const char* p) { return p + '\xff'; }
const char* multiple_bytes(const char* p) { return p + 'ab'; }
const char* multiple_small(const char* p) { return p + '\0x'; }
const char* wide_small(const char* p) { return p + L'x'; }
const char* wide_large(const char* p) { return p + L'\x1234'; }
const char* utf16_small(const char* p) { return p + u'x'; }
const char* utf16_large(const char* p) { return p + u'\u1234'; }
const char* utf32_large(const char* p) { return p + U'\U0001F600'; }
const char* utf8_value(const char* p) { return p + u8'x'; }
