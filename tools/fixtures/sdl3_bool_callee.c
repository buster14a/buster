// Trusted external bool ABI observer; compiled independently with Clang.
#include <stdbool.h>
extern void* sdl_bool_pointer(void);
bool sdl_foreign_false(void)
{
    return sdl_bool_pointer() == 0;
}
