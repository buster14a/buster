extern int x;constexpr int y=_Generic(x,int:0,default:x<<32);
