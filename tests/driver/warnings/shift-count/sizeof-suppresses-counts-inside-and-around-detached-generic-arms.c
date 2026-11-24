int a(int x){return _Generic(x,int:x,default:sizeof(x<<32));}unsigned long b(int x){return sizeof(_Generic(x,int:x<<32,default:x>>-1));}
