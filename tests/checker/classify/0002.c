_Static_assert(_Generic(__builtin_isfpclass(1.0f, 3), int: 1, default: 0) && _Generic(__builtin_signbitl(1), int: 1, default: 0), "");
_Static_assert(_Generic(__builtin_canonicalizef(1), float: 1, default: 0), "");
_Static_assert(_Generic(__builtin_canonicalize(1.0f), double: 1, default: 0), "");
_Static_assert(_Generic(__builtin_canonicalizel(1.0), long double: 1, default: 0), "");
