#ifndef BUSTER_CODEGEN_CONFIGURATIONS_H
#define BUSTER_CODEGEN_CONFIGURATIONS_H

// The accepted allocator names and optimization spellings. The driver and the
// differential runner consume these lists, so adding a mode also adds a leg.
// -fno-register-allocator is the spelling alias for NONE; no -O means the
// driver's default, which the runner exercises separately.
#define BUSTER_CODEGEN_ALLOCATORS(X) \
    X("none", CODEGEN_REGISTER_ALLOCATOR_NONE) \
    X("mir-stack", CODEGEN_REGISTER_ALLOCATOR_MIR_STACK) \
    X("fast", CODEGEN_REGISTER_ALLOCATOR_FAST) \
    X("quality", CODEGEN_REGISTER_ALLOCATOR_QUALITY)

#define BUSTER_CODEGEN_OPTIMIZATIONS(X) \
    X("-O", 0) \
    X("-O0", 0) \
    X("-O1", 1) \
    X("-O2", 2) \
    X("-O3", 3) \
    X("-Os", 2) \
    X("-Oz", 2) \
    X("-Ofast", 2)

#endif
