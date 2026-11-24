#define USER(x) x
# 1 "fake-system.h" 3
#define SYS(x) (x)
SYS(1) USER(2)
# 5 "main.c"
SYS(3) USER(4)
