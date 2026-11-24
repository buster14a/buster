typedef int I __attribute__((aligned(_Generic(__builtin_ia32_rdpid(),unsigned:8,default:4))));
_Static_assert(_Alignof(I)==8,"attribute query");
