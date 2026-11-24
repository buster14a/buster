void f1(struct S1 *p);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvisibility"
void f2(struct S2 *p);
#pragma clang diagnostic pop
int f3(struct S3 { int a; } p, struct S3 { int b; } q);
