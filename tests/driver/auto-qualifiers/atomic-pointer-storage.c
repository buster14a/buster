int object;
extern _Atomic __auto_type *external = &object;
static _Atomic __auto_type *internal = &object;
restrict _Atomic __auto_type *restricted = &object;

void storage(void)
{
    static _Atomic __auto_type *local = &object;
    *external = *internal;
    *local = *restricted;
}
