#pragma once

// Declarative target-neutral selector rules.  The generated decision tree
// consumes the rows in `machine_selection_generated_rules` in rank order, so
// adding a rule is a data change and keeps FAST's first-legal contract visible
// beside QUALITY's bounded list.
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
