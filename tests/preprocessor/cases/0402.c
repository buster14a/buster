#include <dir//x.h> /* c
*/ junk
mid
#if __has_include(<dir//x.h>) /* c
#define Y 1
*/ && 0
no
#endif
#ifdef Y
Y_defined
#endif
#if __has_include(<dir//x.h>) /* c
*/ || 0
yes
#endif
end
