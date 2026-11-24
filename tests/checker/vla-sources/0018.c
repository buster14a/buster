int n; unsigned long p=sizeof(int(*)[sizeof(int[n])]); unsigned long a=_Alignof(int[sizeof(int[n])]);
unsigned long s=sizeof(sizeof(int[n])); unsigned long side=sizeof(sizeof(int[++n]));
enum { P=sizeof(int(*)[sizeof(int[n])]), A=_Alignof(int[sizeof(int[n])]), S=sizeof(sizeof(int[n])) };
_Static_assert(P==8 && A==4 && S==8, "nested unevaluated array size");
