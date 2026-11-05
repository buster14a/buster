struct Row { int *pointer; int value; };
static int local = 13;
struct Row choice __attribute__((selectany, section(".shared"))) = { &local, 17 };
int extra __attribute__((selectany, section(".shared"))) = 29;
int zero __attribute__((selectany));
const int constant __attribute__((selectany)) = 19;
_Thread_local int tls __attribute__((selectany)) = 23;
_Thread_local int tls_zero __attribute__((selectany));
int override __attribute__((selectany)) = 7;
