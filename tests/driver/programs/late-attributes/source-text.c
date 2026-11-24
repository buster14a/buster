static int f(void) __attribute__((used));
static int f(void){return 1;}
static int f(void) __attribute__((used, constructor(100)));
static int f(void) __attribute__((constructor(200),used));
