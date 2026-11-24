struct _Unwind_Context;
typedef int (*Trace)(struct _Unwind_Context* context, void* argument);
extern int _Unwind_Backtrace(Trace trace, void* argument);
static int frame(struct _Unwind_Context* context, void* argument)
{
    (void)context;
    *(int*)argument += 1;
    return 0;
}
int helper(void)
{
    int frames = 0;
    _Unwind_Backtrace(frame, &frames);
    return frames >= 2 ? 0 : 1;
}
