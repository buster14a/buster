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

// The rule rows themselves.  Ranks are dense and ascending except for the
// fallback, which sorts last by construction.
BUSTER_GLOBAL_LOCAL MachineSelectionGeneratedRule const machine_selection_generated_rules[MACHINE_SELECTION_RULE_COUNT] = {
    [MACHINE_SELECTION_RULE_CONSTANT] = {.id = MACHINE_SELECTION_RULE_CONSTANT, .name = S8_INITIALIZER("constant"), .rank = 0u, .fast_legal = true, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_CONSTANT},
    [MACHINE_SELECTION_RULE_LOCAL] = {.id = MACHINE_SELECTION_RULE_LOCAL, .name = S8_INITIALIZER("local"), .rank = 1u, .fast_legal = true, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_LOCAL},
    [MACHINE_SELECTION_RULE_MEMORY] = {.id = MACHINE_SELECTION_RULE_MEMORY, .name = S8_INITIALIZER("memory"), .rank = 2u, .fast_legal = true, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_MEMORY},
    [MACHINE_SELECTION_RULE_ADDRESS] = {.id = MACHINE_SELECTION_RULE_ADDRESS, .name = S8_INITIALIZER("address"), .rank = 3u, .fast_legal = true, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_ADDRESS},
    [MACHINE_SELECTION_RULE_ARITHMETIC] = {.id = MACHINE_SELECTION_RULE_ARITHMETIC, .name = S8_INITIALIZER("arithmetic"), .rank = 4u, .fast_legal = true, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_ARITHMETIC},
    [MACHINE_SELECTION_RULE_CALL] = {.id = MACHINE_SELECTION_RULE_CALL, .name = S8_INITIALIZER("call"), .rank = 5u, .fast_legal = true, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_CALL},
    [MACHINE_SELECTION_RULE_CONTROL] = {.id = MACHINE_SELECTION_RULE_CONTROL, .name = S8_INITIALIZER("control"), .rank = 6u, .fast_legal = true, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_CONTROL},
    [MACHINE_SELECTION_RULE_VECTOR] = {.id = MACHINE_SELECTION_RULE_VECTOR, .name = S8_INITIALIZER("vector"), .rank = 7u, .fast_legal = true, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_VECTOR},
    [MACHINE_SELECTION_RULE_SCALAR] = {.id = MACHINE_SELECTION_RULE_SCALAR, .name = S8_INITIALIZER("scalar"), .rank = 8u, .fast_legal = true, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_SCALAR},
    [MACHINE_SELECTION_RULE_LEGACY_FALLBACK] = {.id = MACHINE_SELECTION_RULE_LEGACY_FALLBACK, .name = S8_INITIALIZER("legacy-fallback"), .rank = 255u, .fast_legal = false, .quality_legal = true, .match = MACHINE_SELECTION_RULE_MATCH_FALLBACK},
};

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

MachineSelectionDecision machine_selection_rule_select(MachineSelectionRuleContext context, MachineSelectionMode mode,
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
