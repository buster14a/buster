int a = 1 << 31;
#pragma clang diagnostic warning "-Wshift-sign-overflow"
int b = 1 << 31;
#pragma clang diagnostic ignored "-Wshift-sign-overflow"
int c = 1 << 31;
