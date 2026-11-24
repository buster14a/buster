#include <stdio.h>
struct _Unwind_Context;
typedef int (*Trace)(struct _Unwind_Context* context, void* argument);
extern int _Unwind_Backtrace(Trace trace, void* argument);
static int count(struct _Unwind_Context* context, void* argument)
{
    (void)context;
    *(int*)argument += 1;
    return 0;
}
int main(void)
{
    int frames = 0;
    _Unwind_Backtrace(count, &frames);
    printf("%d\n", frames > 0);
    return 0;
}
