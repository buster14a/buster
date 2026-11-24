const int a=3;const int b=a+2;
enum { E=b };
_Static_assert(E==5,"initializer chain");
