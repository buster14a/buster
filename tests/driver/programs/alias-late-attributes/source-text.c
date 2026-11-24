int g(void){return 1;}
int h(void) __attribute__((alias("g")));
int h(void);
int h(void);
int h(void) __attribute__((used));
int h(void);
int h(void) __attribute__((used));
int body(void){return 2;}
int body(void);
int body(void) __attribute__((used));
