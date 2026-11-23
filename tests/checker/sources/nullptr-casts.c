// Null pointer constants cast to nullptr_t (#2914), and nullptr_t objects with static storage: file scope, a static
// local, a thread's, a member and an array's elements, each a null pointer's 8 bytes.
typedef typeof(nullptr) N;
enum E { A };
N g = (N)0;
N h = (N)(void *)0;
static N k = (N)false;
N u;
_Thread_local N t = nullptr;
void *v = (N)0;
struct P { int a; N n; long b; } ps = { 1, (N)0, 2 };
N array[3] = { nullptr, (N)0 };
int main(void)
{
    static N s = (N)A;
    N a = (N)0;
    N b = (N)(void *)0;
    N c = (N)(1 - 1);
    N d = (N)'\0';
    N e = (N)(char)0;
    void* p = a;
    _Bool z = (_Bool)(N)0;
    int bad = (g != nullptr) + (h != nullptr) + (k != nullptr) + (u != nullptr) + (t != nullptr) + (v != 0) + (ps.n != nullptr) + (ps.b != 2);
    bad += (array[2] != nullptr) + (s != nullptr) + (b != nullptr) + (c != nullptr) + (d != nullptr) + (e != nullptr) + (p != 0) + z;
    bad += sizeof(N) != 8 || sizeof ps != 24 || sizeof array != 24;
    return bad;
}
