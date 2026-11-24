#define NAME "named.c"
#define FLAG 1
#define EMPTY
# 10 NAME
# 20 "f.c" FLAG
# 30 "main.c" 2
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-line-marker"
# 40 "main.c"
#pragma clang diagnostic pop
# 50 EMPTY "main.c" EMPTY
#pragma clang diagnostic error "-Wgnu"
# 60 "main.c"
#pragma clang diagnostic warning "-Wpedantic"
# 70 "main.c"
#line 80 "main.c"
# 90 "main.c" // comment
#\
 100 "main.c"
# 110
int x;
