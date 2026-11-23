int f(void){__typeof__(__builtin_rotateleft8(1,1)) before;
{double (*__builtin_rotateleft8)(int,int);__typeof__(__builtin_rotateleft8(1,1)) inside;
_Static_assert(_Generic(inside,double:1,default:0),"ordinary shadow");}
__typeof__(__builtin_rotateleft8(1,1)) after;
_Static_assert(_Generic(before,unsigned char:1,default:0),"earlier intrinsic");
_Static_assert(_Generic(after,unsigned char:1,default:0),"restored intrinsic");return 0;}
