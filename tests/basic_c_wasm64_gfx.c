#include <buster/lib/wasm64_gfx.h>

BusterWasm64Window wasm64_gfx_probe(void)
{
    return buster_wasm64_window_create(640, 480, 0);
}
