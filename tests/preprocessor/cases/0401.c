#if __has_include(<dir//x.h>) && 1
yes2
#endif
#if __has_include(<dir//x.h>) && 0
no3
#endif
#if __has_include(<dir//x.h>) || defined(NOPE)
yes4
#endif
#if __has_embed(<dir//x.h> limit(1)) == 1
yes5
#endif
#if __has_embed(<dir//x.h> limit(0)) == 2
yes6
#endif
#include <dir//x.h> junk
end
