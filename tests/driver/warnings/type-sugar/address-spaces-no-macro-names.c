#define MYGS __seg_gs
#define CAS const __attribute__((address_space(1)))
#define ASV __attribute__((address_space(2))) volatile
#define AS3 __attribute__((address_space(3)))
void f(__seg_gs int *a, MYGS int *b, CAS int *c, ASV int *d, AS3 int *e)
{
    a();
    b();
    c();
    d();
    e();
}
