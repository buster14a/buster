// Argument Clinic writes `.kwtuple = (&_kwtuple.ob_base.ob_base)` where
// _kwtuple is a function-local static: the constant evaluator only knew
// file-scope objects, so a local static's member address could not fold,
// and the aggregate path's last resort answered with the symbol at offset
// zero -- every keyword tuple pointed at the wrong subobject.  The
// parenthesized and cast spellings exercise the scalar path's wrapper
// stripping; the checks compare against runtime-computed addresses.
#include <stdio.h>

struct inner
{
    long pad;
    long body;
};

struct outer
{
    struct inner first;
    struct inner second;
};

static struct outer global_object;

static int probe(void)
{
    static struct outer local_object;
    static void* chain = (&local_object.second.body);
    static void* cast_chain = (void*)&local_object.second;
    static void* slots[3] = {
        &local_object.second.body,
        (&local_object.first.body),
        (void*)&global_object.second,
    };
    if (chain != (void*)&local_object.second.body)
    {
        return 1;
    }
    if (cast_chain != (void*)&local_object.second)
    {
        return 2;
    }
    if (slots[0] != (void*)&local_object.second.body || slots[1] != (void*)&local_object.first.body || slots[2] != (void*)&global_object.second)
    {
        return 3;
    }
    return 0;
}

int main(void)
{
    int status = probe();
    if (status)
    {
        return status;
    }
    printf("static local member address ok\n");
    return 0;
}
