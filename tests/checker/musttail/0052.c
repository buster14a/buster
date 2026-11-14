#define P8 ********
#define P63 ******* P8 P8 P8 P8 P8 P8 P8
int f(const int P63 p);
int g(int P63 p)
{
__attribute__((musttail)) return f((const int P63)p);
}
