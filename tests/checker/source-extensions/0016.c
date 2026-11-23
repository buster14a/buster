struct S {int x;};int f(struct S s){return _Generic(__builtin_rotateleft8(s,1),unsigned char:1,default:0);}
