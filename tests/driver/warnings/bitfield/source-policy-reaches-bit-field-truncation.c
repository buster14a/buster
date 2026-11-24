struct S { int a : 1; int b : 3; } s;
void f(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wbitfield-constant-conversion"
    s.a = 1;
    s.b = 9;
#pragma clang diagnostic pop
#pragma clang diagnostic error "-Wsingle-bit-bitfield-constant-conversion"
    s.a = 1;
    s.b = 9;
}
