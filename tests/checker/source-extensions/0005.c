_Static_assert(_Generic(__builtin_ia32_rdpid(),unsigned:1,default:0),"rdpid type");
int g(unsigned char x){return _Generic(__builtin_rotateleft8(x,1),unsigned char:1,default:0);}
