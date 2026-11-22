#define OV __attribute__((overloadable))
int one(int a) OV __attribute__((enable_if(a > 0, "positive")));
int one(double a) OV;
int none(int a) OV __attribute__((enable_if(a > 0, "positive")));
int none(double a) OV __attribute__((enable_if(a > 0, "positive")));
int one(double a) OV { return (int)a; }
void *file_one = one;
void *file_none = none;
int t(void) {
    void *local = &one;
    (void)one;
    int sum = none != 0;
    return sum + (local != 0);
}
