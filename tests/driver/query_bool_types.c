// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Original type-reader reproductions of #2899, with successful and failed assertions.
typedef int Array[(1 << 2) ? 3 : 4];
typedef __typeof__((1 << 2) ? 3 : 4) Query;
_Static_assert(sizeof(int[(1 << 2) ? 3 : 4]) != 0, "array bound succeeds");
_Static_assert(sizeof(__typeof__((1 << 2) ? 3 : 4)) != 0, "typeof succeeds");
_Static_assert(__builtin_types_compatible_p(__typeof__((1 << 2) ? 3 : 4), int) != 0, "types succeeds");
_Static_assert(sizeof(int[(1 << 2) ? 3 : 4]) == 0, "array bound fails");
_Static_assert(sizeof(__typeof__((1 << 2) ? 3 : 4)) == 0, "typeof fails");
_Static_assert(__builtin_types_compatible_p(__typeof__((1 << 2) ? 3 : 4), int) == 0, "types fails");
