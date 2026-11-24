enum E { A=-2, B=A+4, C, N=1<<3 }; int a[N+1]; _Static_assert(sizeof(a)==36 && B==2 && C==3, "enum");
