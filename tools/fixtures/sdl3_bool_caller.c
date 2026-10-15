// SDL-free SDL input-hang reduction. No SDL/server/runtime dependency.
// Each client supplies the same nonnull pointer; the foreign bool is false.
#include <stdbool.h>
#include <stdio.h>
static int sdl_bool_storage;
void* sdl_bool_pointer(void)
{
    return &sdl_bool_storage;
}
extern bool sdl_foreign_false(void);
int main(void)
{
    int observed = sdl_foreign_false() ? 1 : 0;
    printf("SDL_BOOL_PROBE observed=%d expected=0\n", observed);
    return observed;
}
