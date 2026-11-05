// Original probes for K&R and skipped pragma recovery in pinned Clang.
#if CASE == 1
int h(a) int a const; { return a; }
#elif CASE == 2
int h(a,b) int a int b; { return a+b; }
#elif CASE == 3
int h(a) int a { return a; }
#elif CASE == 4
int h(a) int a = 3; { return a; }
#elif CASE == 5
int h(a,b) int a const; int b volatile; { return a+b; }
#elif CASE == 6
int h(a) int a const; int z; { return a; }
#elif CASE == 7
int h(a) int a (3); { return a; }
#elif CASE == 8
int h(a) int a const;
#pragma GCC visibility push(hidden)
{ return a; }
#pragma GCC visibility pop
#elif CASE == 9
int a
#pragma GCC visibility push(hidden)
const;
#pragma GCC visibility pop
#elif CASE == 10
#pragma GCC visibility push(default)
int a
#pragma GCC visibility push(hidden)
const;
#pragma GCC visibility pop
#pragma GCC visibility pop
#elif CASE == 11
int a =
#pragma GCC visibility push(hidden)
;
#pragma GCC visibility pop
#elif CASE == 12
int a +
#pragma GCC visibility push(hidden)
1;
#pragma GCC visibility pop
#elif CASE == 13
int a
#pragma GCC visibility push(hidden
const;
#pragma GCC visibility pop
#elif CASE == 14
int a
#pragma GCC visibility push(unknown)
const;
#pragma GCC visibility pop
#elif CASE == 15
int a
#pragma pack(push,1)
const;
#pragma pack(pop)
#elif CASE == 16
int a
#pragma unknown
const;
#elif CASE == 17
int a
#pragma STDC FENV_ACCESS ON
const;
#elif CASE == 18
int a
#pragma GCC visibility push(hidden)
const;
int b __attribute__((unknown));
#pragma GCC visibility pop
#elif CASE == 19
int a
#pragma GCC visibility pop
const;
#elif CASE == 20
int h(a) int a const; { return a; }
int b __attribute__((unknown));
#endif
