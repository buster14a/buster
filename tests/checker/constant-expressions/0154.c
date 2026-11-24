int x; int f(void) { double x; _Static_assert(_Generic(x,double:1,default:0), "inner identity"); return 1; } _Static_assert(_Generic(x,int:1,default:0), "outer identity");
