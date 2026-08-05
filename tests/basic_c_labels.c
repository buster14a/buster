static int computed_goto_value(int selector)
{
    void *target = (void *)&&dispatch;
    if (selector)
    {
        goto *target;
    }
    return 3;
dispatch:
    return 7;
}

static int computed_goto_conditional(int selector)
{
    goto *(selector ? &&one : &&zero);
zero:
    return 13;
one:
    return 17;
}

static int computed_goto_guarded_mixed(int selector)
{
    void *target = selector ? &&taken : 0;
    if (!selector)
    {
        return 71;
    }
    goto *target;
taken:
    return 73;
}

static int computed_goto_table(int selector)
{
    void *targets[2] = {&&zero, &&one};
    goto *targets[selector & 1];
zero:
    return 19;
one:
    return 23;
}

static int computed_goto_table_copy(int selector)
{
    void *source[2] = {&&zero, &&one};
    void *targets[2] = {source[0], source[1]};
    goto *targets[selector & 1];
zero:
    return 29;
one:
    return 31;
}

static int computed_goto_table_subobject_overwrite(int selector)
{
    void *targets[2];
    targets[0] = &&zero;
    targets[1] = &&one;
    targets[0] = 0;
    goto *targets[selector & 1];
zero:
    return 37;
one:
    return 41;
}

static int computed_goto_dynamic_store(int store_index, int load_index)
{
    void *targets[2] = {&&zero, &&one};
    targets[store_index] = &&two;
    goto *targets[load_index];
zero:
    return 59;
one:
    return 61;
two:
    return 67;
}

static int computed_goto_static_table(int selector)
{
    static void *dispatch[] = {&&zero, &&one};
    goto *dispatch[selector & 1];
zero:
    return 47;
one:
    return 53;
}

static int computed_goto_typedef(void)
{
    typedef void *P;
    P target = (P)&&typed;
    goto *target;
typed:
    return 43;
}

static int asm_goto_value(int selector)
{
    if (selector)
#if defined(__aarch64__) || defined(_M_ARM64)
        __asm__ goto ("b %l1" : : "r"(selector) : "cc" : jump);
#else
        __asm__ goto ("jmp %l1" : : "r"(selector) : "cc" : jump);
#endif
    return 5;
jump:
    return 11;
}

static int asm_goto_call_target(int value)
{
    return value + 29;
}

static int asm_goto_saved_register(int selector)
{
    if (selector)
#if defined(__aarch64__) || defined(_M_ARM64)
        __asm__ goto ("b %l1" : : "r"(selector) : "cc" : saved_register_taken);
#else
        __asm__ goto ("jmp %l1" : : "b"(selector) : "cc" : saved_register_taken);
#endif
    return 31;
saved_register_taken:
    return asm_goto_call_target(selector);
}

static int asm_goto_read_write(int selector)
{
    int value = selector;
#if defined(__aarch64__) || defined(_M_ARM64)
    __asm__ goto ("b %l2" : "+r"(value) : : : read_write_taken);
#else
    __asm__ goto ("jmp %l2" : "+r"(value) : : : read_write_taken);
#endif
    return 37;
read_write_taken:
    return value + 41;
}

static int asm_goto_named(int selector)
{
    int value = selector;
#if defined(__aarch64__) || defined(_M_ARM64)
    __asm__ goto ("b %l[named_taken]" : "+r"(value) : : : named_taken);
#else
    __asm__ goto ("jmp %l[named_taken]" : "+r"(value) : : : named_taken);
#endif
    return 43;
named_taken:
    return value + 47;
}

static int asm_goto_b_fallthrough(int selector)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    __asm__ goto ("" : : "r"(selector) : : never_taken);
#else
    __asm__ goto ("" : : "b"(selector) : : never_taken);
#endif
    return selector + 53;
never_taken:
    return 0;
}

int main(void)
{
    int result = computed_goto_value(1) != 7 || computed_goto_value(0) != 3 || computed_goto_conditional(1) != 17 ||
                 computed_goto_conditional(0) != 13 || computed_goto_table(0) != 19 || computed_goto_table(1) != 23 ||
                 computed_goto_guarded_mixed(0) != 71 || computed_goto_guarded_mixed(1) != 73 ||
                 computed_goto_table_copy(0) != 29 || computed_goto_table_copy(1) != 31 || computed_goto_table_subobject_overwrite(1) != 41 ||
                 computed_goto_dynamic_store(0, 0) != 67 || computed_goto_dynamic_store(0, 1) != 61 ||
                 computed_goto_dynamic_store(1, 0) != 59 || computed_goto_dynamic_store(1, 1) != 67 ||
                 computed_goto_static_table(0) != 47 || computed_goto_static_table(1) != 53 ||
                 computed_goto_typedef() != 43 || asm_goto_value(1) != 11 ||
                 asm_goto_value(0) != 5 || asm_goto_saved_register(1) != 30 || asm_goto_saved_register(0) != 31;
    result |= asm_goto_read_write(1) != 42 || asm_goto_named(1) != 48 || asm_goto_b_fallthrough(7) != 60;
    return result;
}
