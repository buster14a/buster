int f(int x){return (0?_Generic(x,int:0,default:x<<32):0)+(x>>-1);}
