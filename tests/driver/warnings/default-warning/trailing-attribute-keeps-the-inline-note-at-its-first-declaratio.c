inline int g(void) __attribute__((unused));
inline int g(void){static int value;return value;}
