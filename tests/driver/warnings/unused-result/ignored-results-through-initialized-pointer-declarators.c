typedef __attribute__((warn_unused_result)) int wur_int;
int plain_fn(void) { return 0; }
wur_int (*gp)(void) = plain_fn;
static wur_int (*sp)(void) = &plain_fn, (*sq)(void) = 0;
typedef wur_int (*FP)(void);
FP tp = plain_fn;
void test(int n)
{
    wur_int (*fp)(void) = plain_fn;
    fp();
    gp();
    sp();
    sq();
    tp();
    wur_int (*_Nonnull np)(void) = (plain_fn);
    np();
    wur_int (*cp)(void) = n ? plain_fn : plain_fn;
    cp();
    wur_int x = 1, (*dp)(void) = plain_fn;
    dp();
    (void)x;
}
