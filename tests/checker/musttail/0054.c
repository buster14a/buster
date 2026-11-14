#define P8 ********
#define P65 * P8 P8 P8 P8 P8 P8 P8 P8
int f(const int P65 p);
int g(int P65 p)
{
__attribute__((musttail)) return f((const int P65)p);
}
