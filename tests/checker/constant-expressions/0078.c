int x; enum { A=sizeof(__typeof__(x)), B=sizeof(__typeof__(int[3])), C=sizeof(__typeof__((x+1))) }; _Static_assert(A==4 && B==12 && C==4, "typeof");
