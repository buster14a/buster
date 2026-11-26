// Includes contribute definition events just as main-file directives do (#2376).
#include <stddef.h>
#include <stdbool.h>
int *header_null(void) { int *value; return value; }
_Bool header_false(void) { _Bool value; return value; }
#undef NULL
#undef false
int *after_header_undef(void) { int *value; return value; }
_Bool after_false_undef(void) { _Bool value; return value; }
