#define GROUP(e) (e)
#define EQUAL(x) ((x)==1)
int a(int x){if(GROUP(x==1))return 1;return 0;}
int b(int x){if(EQUAL(x))return 1;return 0;}
