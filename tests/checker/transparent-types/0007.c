typedef union __attribute__((transparent_union)){int a[2];}U;typedef void F(U);typedef void I(int*);_Static_assert(!__builtin_types_compatible_p(F,I),"no decay");
