int n; unsigned long c=sizeof(int(*)[n]); unsigned long a=sizeof(int(*)[(3,2)]);
enum { P=sizeof(int(*)[n]), PP=sizeof(int(**)[n]), A=sizeof(int(*[3])[n]), M=sizeof(int(*)[n][n+1]) };
_Static_assert(P==8 && PP==8 && A==24 && M==8, "fixed pointer layout");
