#define BUSTER_UNITY_BUILD 0
#define BUSTER_INCLUDE_TESTS 0
#include <buster/lib/compiler/codegen/machine.c>

_Static_assert(MACHINE_OPCODE_COUNT == 236, "opcode count");
_Static_assert(sizeof(MachineOpcodeInfo) == 96, "opcode metadata size");
_Static_assert(sizeof(MachineX64ValueShape) == 36, "argument shape size");
_Static_assert(sizeof(MachineX64ArgumentPlacement) == 12, "argument placement size");
_Static_assert(sizeof(MachineX64SignaturePlan) == 80, "signature plan size");
_Static_assert(sizeof(((MachineX64Selector*)0)->parameter_shapes) + sizeof(((MachineX64Selector*)0)->parameter_placements) == 1152,
               "selector signature arrays size");
_Static_assert(sizeof(MachineX64PreparedExactOpcode) == 1064, "exact opcode size");
_Static_assert(sizeof(MachineX64GprEncodingTable) == 4102, "GPR table size");
_Static_assert(sizeof(MachineX64VariableMemoryEncodingTable) == 12290, "memory table size");
_Static_assert(sizeof(machine_x64_exact_opcode_map) == 134064, "exact map storage");
_Static_assert(sizeof(machine_x64_gpr_encoding_tables) == 262528, "GPR storage");
_Static_assert(sizeof(machine_x64_variable_memory_encoding_tables) == 196640, "memory storage");
