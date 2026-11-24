int f(int a,int b,int c,int d){return _Generic(0,int:1,default:a<b ^ c<d);}
