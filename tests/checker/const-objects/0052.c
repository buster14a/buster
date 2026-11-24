const int z=0;enum { G=z };
int* q=G;
_Static_assert(G==0,"enumerator zero");
