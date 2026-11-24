void f(void) { goto target; { int a[1&&1],b[1||0],c[1?1:2]; target: a[0]=b[0]=c[0]=0; } }
