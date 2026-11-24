#include <stdio.h>
int __CFConstantStringClassReference[4];
const int* class_reference(void) { return __CFConstantStringClassReference; }
int tested(int);
int main(void) { printf("%d %d\n", tested(3), tested(-2)); return 0; }
