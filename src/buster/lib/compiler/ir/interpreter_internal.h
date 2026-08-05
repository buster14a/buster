#pragma once

#include <buster/lib/compiler/ir/interpreter.h>

typedef struct IrExecutionTarget IrExecutionTarget;
struct IrExecutionTarget
{
    AnalysisResult* analysis;
    IrModule* module;
    IrFunction* function;
};

BUSTER_F_DECL IrExecutionTarget ir_interpreter_function_find(AnalysisProgram* analysis, IrProgram* program, AnalysisEntityId entity,
                                                                  AnalysisInstantiationId instantiation);
BUSTER_F_DECL f64 ir_interpreter_float_read(u64 bits, u32 width);
