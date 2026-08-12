#include <buster/lib/compiler/codegen/machine_select.h>
#include <buster/lib/compiler/codegen/machine_select_rules.h>

typedef struct MachineSelectionGeneratedRule MachineSelectionGeneratedRule;
struct MachineSelectionGeneratedRule
{
    MachineSelectionRuleId id;
    String8 name;
    u8 rank;
    bool fast_legal;
    bool quality_legal;
    MachineSelectionRuleMatch match;
};

#define MACHINE_SELECTION_GENERATED_RULE(rule_id, name_literal, rank_value, fast_value, quality_value, match_value) \
    [rule_id] = {.id = rule_id, .name = S8_INITIALIZER(name_literal), .rank = rank_value, .fast_legal = (fast_value) != 0, \
            .quality_legal = (quality_value) != 0, .match = match_value},
BUSTER_GLOBAL_LOCAL MachineSelectionGeneratedRule const machine_selection_generated_rules[MACHINE_SELECTION_RULE_COUNT] = {
    MACHINE_SELECTION_RULE_SCHEMA(MACHINE_SELECTION_GENERATED_RULE)
};
#undef MACHINE_SELECTION_GENERATED_RULE

BUSTER_GLOBAL_LOCAL bool machine_selection_generated_opcode_is_constant(IrOpcode opcode)
{
    return opcode == IR_OPCODE_CONSTANT_INTEGER || opcode == IR_OPCODE_CONSTANT_FLOAT || opcode == IR_OPCODE_CONSTANT_STRING ||
           opcode == IR_OPCODE_UNDEFINED;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_generated_opcode_is_arithmetic(IrOpcode opcode)
{
    return opcode == IR_OPCODE_CAST || opcode == IR_OPCODE_UNARY || opcode == IR_OPCODE_BINARY || opcode == IR_OPCODE_SIMD;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_generated_opcode_is_control(IrOpcode opcode)
{
    return opcode == IR_OPCODE_BRANCH || opcode == IR_OPCODE_BRANCH_IF || opcode == IR_OPCODE_SWITCH || opcode == IR_OPCODE_INDIRECT_BRANCH ||
           opcode == IR_OPCODE_RETURN || opcode == IR_OPCODE_UNREACHABLE || opcode == IR_OPCODE_DEBUG_TRAP;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_generated_target_accepts(Target target, MachineSelectionRuleMatch match)
{
    switch (match)
    {
        case MACHINE_SELECTION_RULE_MATCH_VECTOR:
            return target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64;
        default:
            return target.cpu_arch < CPU_ARCH_COUNT;
    }
}

BUSTER_GLOBAL_LOCAL bool machine_selection_generated_matches(MachineSelectionGeneratedRule const* rule, MachineSelectionRuleContext context)
{
    if (!rule || !machine_selection_generated_target_accepts(context.target, rule->match))
    {
        return false;
    }
    switch (rule->match)
    {
        case MACHINE_SELECTION_RULE_MATCH_CONSTANT:
            return context.known_constant || machine_selection_generated_opcode_is_constant(context.opcode);
        case MACHINE_SELECTION_RULE_MATCH_LOCAL:
            return context.opcode == IR_OPCODE_LOCAL || context.promotable_local;
        case MACHINE_SELECTION_RULE_MATCH_MEMORY:
            return (context.side_effects & (MACHINE_SELECTION_SIDE_EFFECT_READ_MEMORY | MACHINE_SELECTION_SIDE_EFFECT_WRITE_MEMORY)) != 0;
        case MACHINE_SELECTION_RULE_MATCH_ADDRESS:
            return context.address_candidate;
        case MACHINE_SELECTION_RULE_MATCH_ARITHMETIC:
            return machine_selection_generated_opcode_is_arithmetic(context.opcode) &&
                   (context.result_class & (MACHINE_SELECTION_RESULT_SCALAR | MACHINE_SELECTION_RESULT_VECTOR)) != 0;
        case MACHINE_SELECTION_RULE_MATCH_CALL:
            return context.opcode == IR_OPCODE_CALL || (context.side_effects & MACHINE_SELECTION_SIDE_EFFECT_CALL) != 0;
        case MACHINE_SELECTION_RULE_MATCH_CONTROL:
            return machine_selection_generated_opcode_is_control(context.opcode) ||
                   (context.side_effects & MACHINE_SELECTION_SIDE_EFFECT_CONTROL) != 0;
        case MACHINE_SELECTION_RULE_MATCH_VECTOR:
            return context.vector_features && (context.result_class & MACHINE_SELECTION_RESULT_VECTOR) != 0;
        case MACHINE_SELECTION_RULE_MATCH_SCALAR:
            return (context.result_class & MACHINE_SELECTION_RESULT_SCALAR) != 0 &&
                   (context.side_effects & MACHINE_SELECTION_SIDE_EFFECT_UNKNOWN) == 0;
        case MACHINE_SELECTION_RULE_MATCH_FALLBACK:
            return true;
        case MACHINE_SELECTION_RULE_MATCH_COUNT:
            break;
    }
    return false;
}

BUSTER_F_DECL MachineSelectionDecision machine_selection_rule_select(MachineSelectionRuleContext context, MachineSelectionMode mode,
                                                                      MachineSelectionCounters* counters)
{
    MachineSelectionDecision decision = {
        .mode = mode < MACHINE_SELECTION_MODE_COUNT ? mode : MACHINE_SELECTION_MODE_FAST,
    };
    if (counters)
    {
        counters->query_count[decision.mode] += 1;
    }
    bool selected = false;
    for (u32 rule_index = 1; rule_index < MACHINE_SELECTION_RULE_COUNT; rule_index += 1)
    {
        MachineSelectionGeneratedRule const* rule = machine_selection_generated_rules + rule_index;
        bool mode_legal = decision.mode == MACHINE_SELECTION_MODE_FAST ? rule->fast_legal : rule->quality_legal;
        bool matches = mode_legal && machine_selection_generated_matches(rule, context);
        if (!matches)
        {
            if (counters && rule->id < MACHINE_SELECTION_RULE_COUNT)
            {
                counters->rule_rejections[rule->id] += 1;
            }
            continue;
        }
        MachineSelectionRuleCandidate candidate = {
            .rule = rule->id,
            .status = selected ? MACHINE_SELECTION_RULE_STATUS_QUALITY_ALTERNATIVE : MACHINE_SELECTION_RULE_STATUS_LEGAL,
            .rank = rule->rank,
            .estimated_cost = (u16)(rule->rank + (context.side_effects != MACHINE_SELECTION_SIDE_EFFECT_NONE)),
        };
        if (!selected)
        {
            decision.selected = candidate;
            selected = true;
            if (counters)
            {
                counters->legal_count[decision.mode] += 1;
                counters->rule_hits[rule->id] += 1;
            }
            if (decision.mode == MACHINE_SELECTION_MODE_FAST)
            {
                break;
            }
            continue;
        }
        if (decision.alternative_count < MACHINE_SELECTION_MAX_QUALITY_ALTERNATIVES)
        {
            decision.alternatives[decision.alternative_count++] = candidate;
            if (counters)
            {
                counters->alternative_count[decision.mode] += 1;
                counters->rule_hits[rule->id] += 1;
            }
        }
    }
    if (!selected)
    {
        decision.selected = (MachineSelectionRuleCandidate){
            .rule = MACHINE_SELECTION_RULE_LEGACY_FALLBACK,
            .status = MACHINE_SELECTION_RULE_STATUS_FALLBACK,
            .rank = 255,
            .estimated_cost = UINT16_MAX,
        };
        decision.used_legacy_fallback = true;
        if (counters)
        {
            counters->fallback_count[decision.mode] += 1;
            counters->rule_hits[MACHINE_SELECTION_RULE_LEGACY_FALLBACK] += 1;
        }
    }
    return decision;
}
