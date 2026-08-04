#pragma once

#include <buster/lib/base.h>
#include <buster/lib/target.h>

typedef struct CpuId CpuId;
struct CpuId
{
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
};

typedef struct X86_64CpuFeatureInput X86_64CpuFeatureInput;
struct X86_64CpuFeatureInput
{
    // These are the maximum leaves returned by CPUID(0) and CPUID(80000000h).
    // The leaf structures are intentionally supplied by value so the decoder
    // can be tested without executing CPUID or XGETBV.
    u32 maximum_basic_leaf;
    u32 maximum_extended_leaf;
    CpuId basic;
    CpuId extended_basic;
    CpuId leaf_7_0;
    CpuId leaf_7_1;
    CpuId leaf_1e_0;
    CpuId leaf_1e_1;
    CpuId leaf_24_0;
    CpuId leaf_24_1;
    CpuId leaf_29_0;
    u64 xcr0;
};

typedef enum X86_64RegisterOperation
{
    X86_64_REGISTER_OPERATION_MOVE,
    X86_64_REGISTER_OPERATION_ADD,
    X86_64_REGISTER_OPERATION_SUBTRACT,
    X86_64_REGISTER_OPERATION_AND,
    X86_64_REGISTER_OPERATION_OR,
    X86_64_REGISTER_OPERATION_XOR,
    X86_64_REGISTER_OPERATION_COUNT,
} X86_64RegisterOperation;

typedef struct X86_64EncodedInstruction X86_64EncodedInstruction;
struct X86_64EncodedInstruction
{
    u8 bytes[8];
    u8 length;
};

BUSTER_F_DECL CpuModel cpu_detect_model_x86_64(void);
BUSTER_F_DECL TargetCpuFeatures x86_64_cpu_features_from_cpuid(X86_64CpuFeatureInput input);
BUSTER_F_DECL TargetCpuFeatures cpu_detect_features_x86_64(void);
BUSTER_F_DECL X86_64EncodedInstruction x86_64_encode_register_operation(X86_64RegisterOperation operation, u32 target_register, u32 source_register);
