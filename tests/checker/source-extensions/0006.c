#define T(e,t) _Static_assert(_Generic((e),t:1,default:0),"intrinsic type")
T(__builtin_rotateright8(1,1),unsigned char);T(__builtin_rotateleft16(1,1),unsigned short);
T(__builtin_rotateright32(1,1),unsigned);T(__builtin_rotateleft64(1,1),unsigned long);
T(__builtin_rotateleft8(__builtin_rotateright16(1,1),1),unsigned char);
T(+__builtin_rotateleft8(1,1),int);
