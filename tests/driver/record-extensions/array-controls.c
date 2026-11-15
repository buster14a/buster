const int zero = 0;
int folded_constant[zero];
int folded_comma[(0, 0)];
struct Folded { int member[zero]; };
extern int incomplete[];
int positive[1];
char terminated[] = "";
struct Flexible { int member; int tail[]; };
void local(int length)
{
    const int bound = 0;
    int variable[bound];
    int computed[length - length];
    (void)variable;
    (void)computed;
}
