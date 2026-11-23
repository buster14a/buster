#define ID(x) x
#define HI __has_include
#define ARG <  x.h>
#if __has_include(<  x.h>)
raw
#endif
#if HI(<  x.h>)
hi
#endif
#if ID(__has_include(<  x.h>))
argument
#endif
#if __has_include(ARG)
macro
#endif
#if __has_embed(<  x.h>) == 0 && ID(__has_embed(<  x.h>)) == 1
embed
#endif
#if __has_include(<  x.h>)
#include <  x.h>
#endif
valid_translation_unit
