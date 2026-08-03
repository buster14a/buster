#include <stddef.h>

static size_t fuzz_value;

int main(void)
{
    return (int)fuzz_value;
}
