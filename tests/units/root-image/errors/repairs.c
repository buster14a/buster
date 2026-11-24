// A malformed statement inside a do's body: the checker reads what came before it through a copy of the preprocessor,
// which a root can't take back.
#include "../probe.h"
int repairs_called(int);
void repairs(void)
{
    do
    {
        repairs_called(1 << 44);
        repairs_called(2) +;
    }
    while (0);
}
