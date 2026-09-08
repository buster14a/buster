// All configurations must reject an invalid scalar assignment consistently.
int main(void) { struct S { int x; }; struct S value; value = 1; return 0; }
