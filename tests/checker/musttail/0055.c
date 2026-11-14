#define P8 ********
#define P64 P8 P8 P8 P8 P8 P8 P8 P8
int f(const long P64 p);
int g(int P64 p)
{
__attribute__((musttail)) return f((const long P64)p);
}
