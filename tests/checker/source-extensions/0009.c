enum { N=_Generic(__builtin_ia32_rdpid(),unsigned:3,default:1) };
int a[_Generic(__builtin_rotateleft8(1,1),unsigned char:4,default:1)];
_Static_assert(N==3 && sizeof(a)==4*sizeof(int),"declaration bounds");
