int f(void){ {enum { A=_Generic(__builtin_rotateleft8(1,1),unsigned char:7,default:0)+sizeof(struct S {enum {__builtin_rotateleft8=5} x;}) };
_Static_assert(A==7+sizeof(struct S),"captured before suspended tag");
_Static_assert(__builtin_rotateleft8==5,"tag introduced the shadow");}
_Static_assert(_Generic(__builtin_rotateleft8(1,1),unsigned char:1,default:0),"restored intrinsic");return 0;}
