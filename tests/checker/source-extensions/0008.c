_Static_assert(_Generic(__builtin_ia32_rdpid(),unsigned:1,default:0),"rdpid");
_Static_assert(_Generic(__builtin_rotateleft8(1,1),unsigned char:1,default:0),"rotate");
