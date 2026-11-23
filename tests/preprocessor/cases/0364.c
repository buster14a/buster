#define E
a1
#embed "hi.txt" limit(1)
b1 b2
a2
#embed "hi.txt" limit(1)
   b3
a3
#embed "hi.txt" limit(1)

b4
a5
#embed "hi.txt" limit(1)
/* c
*/ b5
a6
#embed "hi.txt" limit(1) // c
b6
a7
#embed "hi.txt" limit(1)
#define Q
b7
a8
#embed "hi.txt" limit(1)
E b8
a9
#embed "empty.txt"
b9
a10
#embed "empty.txt" if_empty(z)
b10
a11
#include "embedlast.h"
b11
a12
#include "embedlast_nonl.h"
b12
a13
#embed "hi.txt" limit(1)
E
b13
