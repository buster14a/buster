int n; void *p = (int(*)[n])0;
int n2; int s = (int)(long)(int(*)[n2])0;
int n3; int s3 = _Alignof(int[n3]);
_Static_assert(_Alignof(long[n3]) == 8, "");
int s4 = _Alignof(int[n3][4]) + sizeof(int);
char *bad = (int(*)[n])0;
int s5 = sizeof(int[n]);
