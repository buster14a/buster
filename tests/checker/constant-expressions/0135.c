struct S{int x;}; int f(struct S*x){return _Generic(x,struct S*:7,struct S**:2,struct S(*)[3]:3);}
