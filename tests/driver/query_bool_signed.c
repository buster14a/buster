// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Nonliteral signed shifts use the opt-in family; unsigned ones and explicit casts stay quiet.
int signed_size(int n) { return sizeof((n << 2) ? 3 : 4); }
int signed_selection(int n) { return _Generic(0, int: 1, default: (n << 2) ? 3 : 4); }
int signed_type(int n) { typedef __typeof__((n << 2) ? 3 : 4) Query; return sizeof(Query); }
int unsigned_size(unsigned n) { return sizeof((n << 2) ? 3 : 4); }
int cast_size(int n) { return sizeof((_Bool)(n << 2) ? 3 : 4); }
int zero_shift(int n) { return sizeof((0 << n) ? 3 : 4); }
int literal_groups(void) { return sizeof(((1) << (2)) ? 3 : 4); }
