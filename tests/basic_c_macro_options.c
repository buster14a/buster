#ifdef ORDERED
ORDERED
#else
ORDERED_ABSENT
#endif

#ifdef REPEAT
REPEAT
#endif

#ifdef IMPLICIT
IMPLICIT
#endif

empty_begin EMPTY empty_end

#ifdef __GNUC__
__GNUC__
#else
GNUC_ABSENT
#endif

#ifdef __clang__
CLANG_PRESENT
#else
CLANG_ABSENT
#endif

#ifdef DOUBLE
DOUBLE(21)
#endif

#ifdef ZERO
ZERO()
#endif

#ifdef MULTI
MULTI(2, 3)
#endif

#ifdef NESTED
NESTED(2)
#endif

#ifdef STRINGIFY
STRINGIFY(two words)
#endif

#ifdef PASTE
PASTE(join, ed)
#endif

#ifdef VARIADIC
VARIADIC(1, 2 + 3)
#endif

#ifdef EQUALS
EQUALS(1)
#endif

#ifdef SPACED
SPACED(3)
#endif

#ifdef EMPTY_FUNCTION
empty_function_begin EMPTY_FUNCTION(ignored) empty_function_end
#endif
