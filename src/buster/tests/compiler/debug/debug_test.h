#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/debug/debug.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL void debug_variable_add_location(Arena* arena, DebugModelInput* input, DebugVariable* variable, IrSymbolId symbol,
                                                    IrLocalId local, u32 start, u32 end);
BUSTER_F_DECL DebugScopeId debug_scope_add(Arena* arena, DebugModel* model, DebugScopeId parent, DebugScopeKind kind,
                                                 DebugSourceLocation declaration, u32 start, u32 end, u32 variable_capacity);
BUSTER_F_DECL DebugVariableId debug_variable_add(Arena* arena, DebugModel* model, DebugModelInput* input, DebugScope* scope,
                                                       String8 name, DebugTypeId type, DebugSourceLocation declaration,
                                                       DebugVariableKind kind, IrSymbolId symbol, IrLocalId local, u32 start, u32 end);
BUSTER_F_DECL UnitTestResult debug_model_tests(UnitTestArguments* arguments);
#endif
