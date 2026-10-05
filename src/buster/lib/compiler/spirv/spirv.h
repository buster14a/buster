#pragma once

// Direct canonical-IR SPIR-V 1.5 emission for the bounded Vulkan 1.2 compute
// contract in docs/spirv-compute.md. No external compiler or native ABI lowering.

#include <buster/lib/compiler/ir/ir.h>

typedef struct SpirvArtifact SpirvArtifact;
struct SpirvArtifact
{
    ByteSlice bytes;
    String8 diagnostic;
    IrFunctionId function;
    IrInstructionId instruction;
    bool success;
};

BUSTER_F_DECL SpirvArtifact spirv_emit(Arena* arena, IrProgram* program, IrModule* module);
