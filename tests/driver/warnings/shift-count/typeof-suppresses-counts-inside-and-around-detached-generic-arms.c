int a(int x){return _Generic(x,int:x,default:_Alignof(__typeof__(x>>-1)));}unsigned long b(int x){return _Alignof(__typeof__(_Generic(x,int:x<<32,default:x>>-1)));}
