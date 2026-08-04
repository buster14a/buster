static int cleanup_events[64];
static int cleanup_event_count;

static int cleanup_int(int *value)
{
    cleanup_events[cleanup_event_count++] = *value;
    return *value;
}

static void cleanup_const(const int *value)
{
    cleanup_events[cleanup_event_count++] = *value;
}

static void cleanup_void(void *pointer)
{
    cleanup_events[cleanup_event_count++] = *(int *)pointer;
}

static void cleanup_variadic(int *value, ...)
{
    cleanup_events[cleanup_event_count++] = *value;
}

static void cleanup_array(int value[1])
{
    cleanup_events[cleanup_event_count++] = *value;
}

static int cleanup_fallthrough(void)
{
    int start = cleanup_event_count;
    {
        int first __attribute__((cleanup(cleanup_int))) = 1;
        int second __attribute__((cleanup(cleanup_int))) = 2;
    }
    return cleanup_event_count == start + 2 && cleanup_events[start] == 2 && cleanup_events[start + 1] == 1;
}

static int cleanup_nested(void)
{
    int start = cleanup_event_count;
    {
        int outer __attribute__((cleanup(cleanup_int))) = 3;
        {
            int inner_first __attribute__((cleanup(cleanup_int))) = 4;
            int inner_second __attribute__((cleanup(cleanup_int))) = 5;
        }
        if (cleanup_event_count != start + 2 || cleanup_events[start] != 5 || cleanup_events[start + 1] != 4)
        {
            return 0;
        }
    }
    return cleanup_event_count == start + 3 && cleanup_events[start + 2] == 3;
}

static int cleanup_return(void)
{
    int value __attribute__((cleanup(cleanup_int))) = 6;
    return value;
}

static int cleanup_block_scope_extern(void)
{
    int start = cleanup_event_count;
    int result = 0;
    {
        extern int cleanup_int(int *);
        int value __attribute__((cleanup(cleanup_int))) = 14;
        result = value;
    }
    return cleanup_event_count == start + 1 && cleanup_events[start] == 14 ? result : 0;
}

static int cleanup_loop_exits(void)
{
    int start = cleanup_event_count;
    int index = 0;
    for (; index < 3; index += 1)
    {
        int value __attribute__((cleanup(cleanup_int))) = index + 10;
        if (index == 1)
        {
            continue;
        }
        if (index == 2)
        {
            break;
        }
    }
    return cleanup_event_count == start + 3 && cleanup_events[start] == 10 && cleanup_events[start + 1] == 11 && cleanup_events[start + 2] == 12;
}

static int cleanup_switch_continue(void)
{
    int start = cleanup_event_count;
    int index = 0;
    for (int outer __attribute__((cleanup(cleanup_int))) = 100; index < 2; index += 1)
    {
        switch (index)
        {
        case 0:
        {
            int inner __attribute__((cleanup(cleanup_int))) = 10;
            continue;
        }
        case 1:
        {
            int inner __attribute__((cleanup(cleanup_int))) = 20;
            break;
        }
        }
        break;
    }
    return cleanup_event_count == start + 3 && cleanup_events[start] == 10 && cleanup_events[start + 1] == 20 && cleanup_events[start + 2] == 100;
}

static int cleanup_for_header(void)
{
    int start = cleanup_event_count;
    for (int header __attribute__((cleanup(cleanup_int))) = 30; header < 32; header += 1)
    {
        int body __attribute__((cleanup(cleanup_int))) = header;
        if (header == 30)
        {
            continue;
        }
        break;
    }
    return cleanup_event_count == start + 3 && cleanup_events[start] == 30 && cleanup_events[start + 1] == 31 && cleanup_events[start + 2] == 31;
}

static int cleanup_goto(void)
{
    int start = cleanup_event_count;
    {
        int value __attribute__((cleanup(cleanup_int))) = 13;
        goto outside;
    }
outside:
    return cleanup_event_count == start + 1 && cleanup_events[start] == 13;
}

static int cleanup_unreached(void)
{
    int start = cleanup_event_count;
    if (0)
    {
        int value __attribute__((cleanup(cleanup_int))) = 14;
    }
    return cleanup_event_count == start;
}

static int cleanup_uninitialized(void)
{
    int start = cleanup_event_count;
    {
        int value __attribute__((cleanup(cleanup_int)));
        value = 15;
    }
    return cleanup_event_count == start + 1 && cleanup_events[start] == 15;
}

static int cleanup_pointer_conversions(void)
{
    int start = cleanup_event_count;
    {
        int exact __attribute__((__cleanup__(cleanup_int))) = 16;
        int qualified __attribute__((cleanup(cleanup_const))) = 17;
        int erased __attribute__((cleanup(cleanup_void))) = 18;
        int variadic __attribute__((cleanup(cleanup_variadic))) = 19;
        int array_parameter __attribute__((cleanup(cleanup_array))) = 20;
    }
    return cleanup_event_count == start + 5 && cleanup_events[start] == 20 && cleanup_events[start + 1] == 19 && cleanup_events[start + 2] == 18 &&
           cleanup_events[start + 3] == 17 && cleanup_events[start + 4] == 16;
}

int main(void)
{
    if (!cleanup_fallthrough() || !cleanup_nested() || cleanup_return() != 6 || cleanup_block_scope_extern() != 14 || !cleanup_loop_exits() ||
        !cleanup_goto() || !cleanup_unreached() || !cleanup_uninitialized() || !cleanup_pointer_conversions())
    {
        return 1;
    }
    int switch_start = cleanup_event_count;
    if (!cleanup_switch_continue() || cleanup_event_count != switch_start + 3 || cleanup_events[switch_start] != 10 ||
        cleanup_events[switch_start + 1] != 20 || cleanup_events[switch_start + 2] != 100)
    {
        return 1;
    }
    int for_start = cleanup_event_count;
    if (!cleanup_for_header() || cleanup_event_count != for_start + 3 || cleanup_events[for_start] != 30 || cleanup_events[for_start + 1] != 31 ||
        cleanup_events[for_start + 2] != 31)
    {
        return 1;
    }
    return 0;
}
