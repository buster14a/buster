int n; enum { A=_Alignof(int[n]), P=_Alignof(int(*)[n]) }; _Static_assert(A==4 && P==8, "fixed alignment");
