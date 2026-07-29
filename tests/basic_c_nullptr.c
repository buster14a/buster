#include <stddef.h>

nullptr_t global_null = nullptr;
int* global_pointer = nullptr;

static int accepts_pointer(int* pointer)
{
    return pointer == nullptr;
}

static int accepts_nullptr(nullptr_t value)
{
    return value == nullptr;
}

int main(void)
{
    nullptr_t local = nullptr;
    nullptr_t copy = local;
    int* pointer = nullptr;
    int* conditional = true ? pointer : nullptr;
    int* reverse_conditional =
        false ? nullptr : pointer;
    int* integer_zero_conditional =
        true ? 0 : pointer;
    int* reverse_integer_zero_conditional =
        false ? pointer : (4 - 4);
    nullptr_t null_conditional =
        false ? nullptr : (2 - 2);
    void* explicit_pointer = (void*)nullptr;
    bool explicit_boolean = (bool)nullptr;
    nullptr_t explicit_nullptr =
        (nullptr_t)nullptr;

    if (nullptr || local || pointer ||
        conditional || reverse_conditional ||
        integer_zero_conditional ||
        reverse_integer_zero_conditional ||
        null_conditional || explicit_pointer ||
        explicit_boolean || explicit_nullptr)
    {
        return 1;
    }
    if (global_null != nullptr ||
        global_pointer != nullptr)
    {
        return 2;
    }
    if (nullptr != pointer ||
        !(nullptr == pointer) ||
        nullptr != 0 ||
        local != (1 - 1) ||
        pointer != (3 - 3))
    {
        return 3;
    }
    if (!accepts_pointer(nullptr) ||
        !accepts_nullptr(copy))
    {
        return 4;
    }
    if (_Generic(
            nullptr,
            nullptr_t: 1,
            default: 0) != 1)
    {
        return 5;
    }
    if (_Generic(
            pointer,
            nullptr_t: 0,
            int*: 1,
            default: 0) != 1)
    {
        return 6;
    }
    if (sizeof(nullptr_t) != sizeof(void*) ||
        _Alignof(nullptr_t) !=
            _Alignof(void*))
    {
        return 7;
    }
    local = nullptr;
    return local == nullptr ? 0 : 8;
}
