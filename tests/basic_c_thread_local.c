_Thread_local int thread_local_value = 7;
_Thread_local int thread_local_zero;

int main(void)
{
    thread_local_value += 5;
    thread_local_zero += 3;
    if (thread_local_value != 12)
    {
        return thread_local_value;
    }
    if (thread_local_zero != 3)
    {
        return 100 + thread_local_zero;
    }
    return 0;
}
