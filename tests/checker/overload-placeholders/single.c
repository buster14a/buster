#define OV __attribute__((overloadable))
int one(int a) OV __attribute__((enable_if(a > 0, "positive")));
int one(double a) OV;
int one(double a) OV { return (int)a + 1; }
void *file_one = one;
void *address(void) {
    void *local = &one;
    (void)one;
    return local == file_one ? local : 0;
}
