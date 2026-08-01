typedef unsigned long test_u64;
typedef unsigned char test_u8;

struct TestAlignofConditional
{
    char padding[__alignof__(void*) < sizeof(short) ? sizeof(short) : __alignof__(void*)];
};

_Static_assert(sizeof(struct TestAlignofConditional) == 8, "sizeof and GNU alignof are constant array-bound operands");

struct TestSigcontext
{
    test_u64 fault_address;
    test_u64 regs[31];
    test_u64 sp;
    test_u64 pc;
    test_u64 pstate;
    test_u8 reserved[4096] __attribute__((aligned(16)));
};

_Static_assert(_Alignof(struct TestSigcontext) == 16, "GNU aligned member controls aggregate alignment");

typedef struct TestSigcontext TestMcontext;

typedef struct TestUcontext
{
    unsigned long flags;
    void* link;
    unsigned char stack[24];
    union
    {
        unsigned long mask;
        unsigned long mask64;
    };
    char padding[120];
    TestMcontext context;
} TestUcontext;

test_u64 context_pc(TestUcontext* context)
{
    return context->context.pc;
}

union TestSifields
{
    struct
    {
        void* address;
    } fault;
};

typedef struct TestSiginfo
{
    union
    {
        struct
        {
            int signal;
            int error;
            int code;
            union TestSifields fields;
        };
        int padding[32];
    };
} TestSiginfo;

void* fault_address(TestSiginfo* info)
{
    return info->fields.fault.address;
}

typedef int TestGetEnv(void* table, void** environment, int version);

struct TestInvokeTable
{
    TestGetEnv* get_env;
};

int invoke_get_env(struct TestInvokeTable** table, void** environment)
{
    return (*table)->get_env(table, environment, 6);
}

struct TestForwardInvokeTable;
typedef const struct TestForwardInvokeTable* TestForwardEnvironment;

struct TestForwardInvokeTable
{
    int (*get_version)(TestForwardEnvironment*);
    const unsigned short* (*get_chars)(TestForwardEnvironment*, void*);
};

int invoke_forward_get_version(TestForwardEnvironment* environment)
{
    return (*environment)->get_version(environment);
}
