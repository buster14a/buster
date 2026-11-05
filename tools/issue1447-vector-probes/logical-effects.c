typedef int V __attribute__((ext_vector_type(4)));
static int state;
V vector(int n) { state = state * 10 + n; return (V){0,1,-1,2}; }
int scalar(int n) { state = state * 10 + n; return n == 1 ? 0 : 1; }
int and_vv(void) { state=0; V r = vector(1) && vector(2); return state+(r[0]+r[1])*100; }
int and_sv(void) { state=0; V r = scalar(1) && vector(2); return state+(r[0]+r[1])*100; }
int or_sv(void) { state=0; V r = scalar(2) || vector(3); return state+(r[0]+r[1])*100; }
int ternary(void) { state=0; V r = vector(1) ? vector(2) : vector(3); return state+(r[0]+r[1])*100; }
extern int printf(const char *, ...);
int main(void) { printf("and_vv=%d\nand_sv=%d\nor_sv=%d\nternary=%d\n",and_vv(),and_sv(),or_sv(),ternary()); return 0; }
