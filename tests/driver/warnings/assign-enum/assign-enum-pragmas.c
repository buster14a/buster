#pragma clang diagnostic warning "-Wassign-enum"
enum A { A1, A2 } a;
void f(void) { a = 5; }
#pragma clang diagnostic ignored "-Wassign-enum"
void g(void) { a = 5; }
