void f1(struct S1 *p);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvisibility"
void f2(struct S2 *p);
#pragma clang diagnostic pop
void f3(struct S3 *p, struct S3 { int a; } *q);
