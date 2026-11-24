int f(void){return _Generic(__builtin_ia32_rdpid(1),unsigned:1,default:0);}
