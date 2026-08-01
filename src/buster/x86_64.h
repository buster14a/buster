#pragma once

#include <buster/base.h>
#include <buster/target.h>

typedef struct CpuId CpuId;
struct CpuId
{
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
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
BUSTER_F_DECL TargetCpuFeatures cpu_detect_features_x86_64(void);
BUSTER_F_DECL X86_64EncodedInstruction x86_64_encode_register_operation(X86_64RegisterOperation operation, u32 target_register, u32 source_register);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult x86_64_tests(UnitTestArguments* arguments);
#endif
