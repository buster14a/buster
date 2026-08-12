#pragma once

// Declarative target-neutral selector rules.  The generated decision tree
// consumes these rows in rank order, so adding a rule is a data change and
// keeps FAST's first-legal contract visible beside QUALITY's bounded list.
#define MACHINE_SELECTION_RULE_SCHEMA(X)                                                                                                  \
    X(MACHINE_SELECTION_RULE_CONSTANT, "constant", 0u, 1u, 1u, MACHINE_SELECTION_RULE_MATCH_CONSTANT)                                  \
    X(MACHINE_SELECTION_RULE_LOCAL, "local", 1u, 1u, 1u, MACHINE_SELECTION_RULE_MATCH_LOCAL)                                          \
    X(MACHINE_SELECTION_RULE_MEMORY, "memory", 2u, 1u, 1u, MACHINE_SELECTION_RULE_MATCH_MEMORY)                                        \
    X(MACHINE_SELECTION_RULE_ADDRESS, "address", 3u, 1u, 1u, MACHINE_SELECTION_RULE_MATCH_ADDRESS)                                      \
    X(MACHINE_SELECTION_RULE_ARITHMETIC, "arithmetic", 4u, 1u, 1u, MACHINE_SELECTION_RULE_MATCH_ARITHMETIC)                              \
    X(MACHINE_SELECTION_RULE_CALL, "call", 5u, 1u, 1u, MACHINE_SELECTION_RULE_MATCH_CALL)                                              \
    X(MACHINE_SELECTION_RULE_CONTROL, "control", 6u, 1u, 1u, MACHINE_SELECTION_RULE_MATCH_CONTROL)                                     \
    X(MACHINE_SELECTION_RULE_VECTOR, "vector", 7u, 1u, 1u, MACHINE_SELECTION_RULE_MATCH_VECTOR)                                         \
    X(MACHINE_SELECTION_RULE_SCALAR, "scalar", 8u, 1u, 1u, MACHINE_SELECTION_RULE_MATCH_SCALAR)                                         \
    X(MACHINE_SELECTION_RULE_LEGACY_FALLBACK, "legacy-fallback", 255u, 0u, 1u, MACHINE_SELECTION_RULE_MATCH_FALLBACK)

typedef enum MachineSelectionRuleMatch
{
    MACHINE_SELECTION_RULE_MATCH_CONSTANT,
    MACHINE_SELECTION_RULE_MATCH_LOCAL,
    MACHINE_SELECTION_RULE_MATCH_MEMORY,
    MACHINE_SELECTION_RULE_MATCH_ADDRESS,
    MACHINE_SELECTION_RULE_MATCH_ARITHMETIC,
    MACHINE_SELECTION_RULE_MATCH_CALL,
    MACHINE_SELECTION_RULE_MATCH_CONTROL,
    MACHINE_SELECTION_RULE_MATCH_VECTOR,
    MACHINE_SELECTION_RULE_MATCH_SCALAR,
    MACHINE_SELECTION_RULE_MATCH_FALLBACK,
    MACHINE_SELECTION_RULE_MATCH_COUNT,
} MachineSelectionRuleMatch;
