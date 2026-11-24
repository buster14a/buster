int a[] = {
#embed "hi.txt"
};
int b[] = {
#embed "bin.dat"
};
int c[] = { 1,
#embed "hi.txt" limit(2) prefix(0,) suffix(,9)
};
int d[] = {
#embed "empty.txt" if_empty(42) prefix(7,)
};
#define LIM 1
int e[] = {
#embed "bin.dat" limit(LIM+1) clang::offset(1)
};
int f[] = {
#embed "hi.txt" __limit__(3) __suffix__(;)
};
int g[] = {
#embed "hi.txt" limit(0) if_empty(0) clang::offset(9)
#embed "hi.txt" clang::offset(4)
#embed "hi.txt" clang::offset(18446744073709551615u) limit(-1u)
};
x
