// Fault payloads for the independent batching harness contract regression.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int shared_state;

int native_frame_case_0(void)
{
    int result = 0;
    char const* fault = getenv("BUSTER_NATIVE_FRAME_BATCH_FAULT");
    if (fault)
    {
        if (strcmp(fault, "fail") == 0) { result = 7; }
        else if (strcmp(fault, "crash") == 0) { raise(SIGILL); result = 1; }
        else if (strcmp(fault, "timeout") == 0 || strcmp(fault, "cancel") == 0)
        {
            volatile unsigned count = 0;
            for (;;) { count += 1; }
        }
        else if (strcmp(fault, "partial") == 0) { _Exit(0); }
        else if (strcmp(fault, "malformed") == 0) { puts("unexpected record"); }
        else if (strcmp(fault, "leak") == 0) { shared_state = 1; }
    }
    return result;
}

int native_frame_case_1(void) { return shared_state; }
int native_frame_case_2(void) { return 0; }
int native_frame_case_4(void) { return 0; }
int native_frame_case_5(void) { return 0; }
int native_frame_case_7(void) { return 0; }
int native_frame_case_8(void) { return 0; }
int native_frame_case_9(void) { return 0; }
