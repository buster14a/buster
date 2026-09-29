// Independent Clang entry harness for the opt-in native-frame batching pilot.
// Every payload and observer remains a separate TU; no LTO or inlining across
// those boundaries. stdout is an exact ordered manifest, flushed before calls.
#include <fenv.h>
#include <stdio.h>
#include <string.h>

#if defined(BUSTER_NATIVE_FRAME_BATCH_SINGLE)
int native_frame_case_9(void);

int main(void)
{
    return native_frame_case_9();
}
#else
int native_frame_case_0(void);
int native_frame_case_1(void);
int native_frame_case_2(void);
int native_frame_case_4(void);
int native_frame_case_5(void);
int native_frame_case_7(void);
int native_frame_case_8(void);
int native_frame_case_9(void);

int main(int argc, char** argv)
{
    int result = 0;
    fenv_t initial;
    int saved = argc == 9 && fegetenv(&initial) == 0;
    if (!saved)
    {
        result = 1;
    }
    for (int index = 1; !result && index < argc; index += 1)
    {
        if (!argv[index][0] || strpbrk(argv[index], " \r\n\t")) { result = 1; }
        for (int prior = 1; prior < index; prior += 1)
        {
            if (strcmp(argv[index], argv[prior]) == 0) { result = 1; }
        }
    }
    for (int index = 0; !result && index < 8; index += 1)
    {
        if (fesetenv(&initial) != 0 || printf("NATIVE_FRAME_BATCH_V1 start %s\n", argv[index + 1]) < 0 || fflush(stdout) != 0)
        {
            result = 1;
        }
        else
        {
            int case_result;
            switch (index)
            {
                case 0: case_result = native_frame_case_0(); break;
                case 1: case_result = native_frame_case_1(); break;
                case 2: case_result = native_frame_case_2(); break;
                case 3: case_result = native_frame_case_4(); break;
                case 4: case_result = native_frame_case_5(); break;
                case 5: case_result = native_frame_case_7(); break;
                case 6: case_result = native_frame_case_8(); break;
                default: case_result = native_frame_case_9(); break;
            }
            // Stop on the first failure; the parent records later IDs as
            // unexecuted and never repairs the failed batch through replay.
            if (printf("NATIVE_FRAME_BATCH_V1 complete %s %d\n", argv[index + 1], case_result) < 0 || fflush(stdout) != 0 || case_result)
            {
                result = 1;
            }
        }
    }
    if (saved && fesetenv(&initial) != 0) { result = 1; }
    return result;
}
#endif
