// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Original reproductions of #2899: successful expressions must keep erased source conditions.
int sizes(void) { return sizeof((1 << 2) ? 3 : 4); }
int selections(void) { return _Generic((1 << 2) ? 3 : 4, int: 1); }
_Static_assert(sizeof((1 << 2) ? 3 : 4) != 0, "size passes");
_Static_assert(_Generic((1 << 2) ? 3 : 4, int: 1), "generic passes");
_Static_assert(_Generic(0, int: 1, default: (1 << 2) ? 3 : 4), "unselected passes");
