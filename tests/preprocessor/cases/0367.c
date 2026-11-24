#embed <x.bin>
#embed "x.bin"
a
#include "sub/inc.h"
b
#include "sub/late.h"
c
#if __has_embed(<x.bin>) == 1 && __has_embed("x.bin") == 1 && __has_embed("y.bin") == 0
found
#endif
