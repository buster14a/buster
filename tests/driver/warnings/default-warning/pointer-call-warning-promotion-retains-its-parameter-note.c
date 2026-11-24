#pragma clang diagnostic error "-Wincompatible-pointer-types-discards-qualifiers"
void f(int *parameter);void g(const int *p){f(p);}
