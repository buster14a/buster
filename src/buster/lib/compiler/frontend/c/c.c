#include "c_internal.h"

// Keep the frontend historical declaration order and one-translation-unit
// linkage while making the large implementation mechanically navigable. The
// same .c files are included here in historical order for unity builds and are
// compiled as independent sources in non-unity builds. Each implementation
// owns its physical source path and line numbers in both modes so debuggers,
// profilers, and diagnostics point at the split file that contains the code.
#include "c_source.c"
#include "c_parse.c"
#include "c_ast_types.c"
#include "c_ast.c"
#include "c_gen.c"
#undef ir_function_add_instruction
