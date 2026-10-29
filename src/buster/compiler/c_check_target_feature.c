// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Code generation's check of target features (#775), as clang 23.1.1 makes it at -c and when it links, but not at
// -fsyntax-only (CodeGenFunction::EmitCall and checkTargetFeatures). A direct call of an always_inline function where
// either function has a target attribute, or a call in a flatten function where either has one, is an error when the
// callee's features hold one its caller's lack:
//
// - where the callee's target attribute applies (one version: no target_clones, "default" or other strings), the first
//   such feature: its attribute's own features first, in the attribute's order, then the rest of its features;
// - otherwise, where the caller's applies, each such feature, an error each.
//
// A function's features are predefines_apply's for its attribute, or the unit's without one (ASTContext::
// getFunctionFeatureMap), and its attributes those of its declarations before the call (the callee's) or up to its
// definition (the caller's), as a declaration's inherited attributes are. The calls are those clang emits: the walk
// of #487's diagnostics (c_check_x86_diagnostic.c) reaches what a constant condition leaves running, not a dead
// branch's or operand's, sizeof's or what follows a return; a callee is direct where it is the function's name alone,
// not `(*f)` or `(&f)` (CodeGenFunction::EmitCallee). Clang's errors are unrecoverable: it emits the functions it must
// emit where the unit defines them (ASTContext::DeclMustBeEmitted: an external definition, by GNU's and C99's rules of
// inline functions, or one with a used, constructor or destructor attribute), and none after the first that has one,
// nor the functions it defers to the unit's end; where none of those has one, it emits every deferred function, past
// their errors. So this reports the first such function's errors alone, or else every deferred function's, by the
// selection's CheckProgram.body_eager, each function's in clang's emission order: by statement, in the lowering's order
// of them (a for loop's increment after its body), and in a statement by the calls' ends (an argument's call before its
// own; but where clang evaluates out of source order, an assignment's right side first). Clang emits the eager
// functions in source order, and the deferred ones depth first from where its emission first referred to them; this
// takes those in source order too. Where the feature an error names is the first of the rest of a function's map (an
// attribute's arch= CPU's, or a caller's attribute that turns several off), clang's order is its StringMap's and this
// one predefines_features', so the two may name another feature for the same call.
//
// The pass is cold and scalar: it runs in a unit with a target attribute alone (CheckProgram.attribute_keys), and walks
// only a body that refers to a callee that may fail, whose call sites it then finds (clang's headers give every intrinsic
// one, but a unit compiled for the features its intrinsics need, the usual one, refers to none that fails).

typedef struct CheckTargetFeatureError CheckTargetFeatureError;
struct CheckTargetFeatureError
{
    // The call's callee's token, where clang reports it (the call's beginning).
    uint32_t token;
    uint32_t caller;
    uint32_t callee;
    // The feature, an index in predefines_features.
    uint32_t feature;
    // err_flatten_function_needs_feature's form, where the callee isn't always_inline: else err_function_needs_feature's.
    bool flatten;
    // Whether clang defers the caller, emitting it after the unit's end (a static or inline function), where its errors go.
    bool deferred;
};

// A function's attributes that the check reads, from its declarations before a token.
typedef struct CheckTargetFeatureAttributes CheckTargetFeatureAttributes;
struct CheckTargetFeatureAttributes
{
    // The target attribute that applies, where it is a version's (predefines_target_attribute's), or null.
    const CheckInputAttribute* version;
    // Whether a target attribute applies: a version's or "default" (clang's hasAttr<TargetAttr>).
    bool targeted;
    // GNU's function multiversioning: target_clones, "default", or target attributes of other strings
    // (FunctionDecl::isMultiVersion).
    bool versions;
    bool always_inline;
    bool flatten;
};

// The maps of the attribute strings the pass has resolved, by their bytes' hash: a unit's attributes have few strings
// (clang's headers give each header's functions one, through a macro), and a body that calls several intrinsics reads
// several of them in turn.
#define BUSTER_CHECK_TARGET_FEATURE_CACHE 64

typedef struct CheckTargetFeatureCached CheckTargetFeatureCached;
struct CheckTargetFeatureCached
{
    String8 text;
    PredefinesFeatures features;
    bool valid;
};

// The walk's filter's answers for callees of a function without a target or flatten attribute of its own, the usual
// caller, whose answer depends on the callee alone: by object, as a body calls few functions, an intrinsic many times.
#define BUSTER_CHECK_TARGET_FEATURE_CALLEES 64

typedef struct CheckTargetFeatureCallee CheckTargetFeatureCallee;
struct CheckTargetFeatureCallee
{
    // The callee's object, or 0 for none.
    uint32_t object;
    bool fails;
};

// The functions' feature maps: the unit's, and those of the attribute strings resolved so far; and the filter's answers.
typedef struct CheckTargetFeatureMaps CheckTargetFeatureMaps;
struct CheckTargetFeatureMaps
{
    const PredefinesOptions* options;
    PredefinesHost host;
    PredefinesFeatures unit;
    CheckTargetFeatureCached cache[BUSTER_CHECK_TARGET_FEATURE_CACHE];
    CheckTargetFeatureCallee callees[BUSTER_CHECK_TARGET_FEATURE_CALLEES];
};

// `object`'s attributes from its declarations before token `limit` (CheckProgram.attribute_keys: each object's target
// attributes' declarations come first, in token order, so the first that applies is the first read).
static CheckTargetFeatureAttributes check_target_feature_attributes(const CheckProgram* program, uint32_t object, uint32_t limit)
{
    CheckTargetFeatureAttributes result = { 0 };
    const uint64_t* keys = program->attribute_keys;
    const uint32_t count = program->attribute_key_count;
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = (uint32_t)(keys[middle] >> 32) < object ? middle + 1 : low;
        high = (uint32_t)(keys[middle] >> 32) < object ? high : middle;
    }
    String8 first_text = { 0 };
    for (uint32_t k = low; k < count && (uint32_t)(keys[k] >> 32) == object; k += 1)
    {
        const uint32_t token = (uint32_t)keys[k];
        const CheckInputAttributes* attributes = token < limit ? program->input->attributes[token] : 0;
        for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
        {
            const CheckInputAttribute* attribute = &attributes->records[i];
            if (!attribute->ignored)
            {
                result.always_inline |= attribute->kind == CHECK_ATTRIBUTE_ALWAYS_INLINE;
                result.flatten |= attribute->kind == CHECK_ATTRIBUTE_FLATTEN;
                result.versions |= attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES;
                int32_t cpu = -1;
                // What clang ignores with a warning isn't the function's attribute at all.
                const PredefinesTargetAttribute kind = attribute->kind == CHECK_ATTRIBUTE_TARGET && attribute->text.length < UINT32_MAX ?
                                                           predefines_target_attribute(attribute->text.pointer, (uint32_t)attribute->text.length, &cpu) :
                                                           PREDEFINES_TARGET_ATTRIBUTE_IGNORED;
                if (kind != PREDEFINES_TARGET_ATTRIBUTE_IGNORED && !result.targeted)
                {
                    result.targeted = true;
                    result.version = kind == PREDEFINES_TARGET_ATTRIBUTE_VERSION ? attribute : 0;
                    result.versions |= kind == PREDEFINES_TARGET_ATTRIBUTE_DEFAULT;
                    first_text = attribute->text;
                }
                else if (kind != PREDEFINES_TARGET_ATTRIBUTE_IGNORED)
                {
                    result.versions |= !check_program_text_equal(attribute->text, first_text);
                }
            }
        }
    }
    return result;
}

// The features of a function of `attributes`: its target attribute's, as predefines_function_target resolves them, or
// the unit's.
static PredefinesFeatures check_target_feature_map(CheckTargetFeatureMaps* maps, const CheckTargetFeatureAttributes* attributes)
{
    PredefinesFeatures result = maps->unit;
    if (attributes->version != 0)
    {
        const String8 text = attributes->version->text;
        uint32_t hash = 2166136261u;
        for (uint64_t i = 0; i < text.length; i += 1)
        {
            hash = (hash ^ (uint8_t)text.pointer[i]) * 16777619u;
        }
        CheckTargetFeatureCached* cached = maps->cache + hash % BUSTER_CHECK_TARGET_FEATURE_CACHE;
        if (!(cached->valid && check_program_text_equal(text, cached->text)))
        {
            int32_t cpu = -1;
            predefines_target_attribute(text.pointer, (uint32_t)text.length, &cpu);
            cached->features = predefines_apply(maps->options, &maps->host, cpu >= 0 ? cpu : maps->host.cpu, text.pointer, (uint32_t)text.length);
            cached->text = text;
            cached->valid = true;
        }
        result = cached->features;
    }
    return result;
}

// Whether `callee` holds a feature `caller` lacks: from feature `from` on, the first such feature, or -1. A word of
// features at a time, as the usual call lacks none.
static int32_t check_target_feature_lacked(PredefinesFeatures callee, PredefinesFeatures caller, uint32_t from)
{
    int32_t result = -1;
    for (uint32_t w = from / 64; w < BUSTER_PREDEFINES_FEATURE_WORDS && result < 0; w += 1)
    {
        const uint64_t lacked = callee.words[w] & ~caller.words[w] & (w == from / 64 ? ~0ull << (from % 64) : ~0ull);
        result = lacked != 0 ? (int32_t)(w * 64 + (uint32_t)__builtin_ctzll(lacked)) : -1;
    }
    return result;
}

// The feature a callee's error names where its target attribute applies, or -1 for none (checkTargetFeatures'
// required features): the attribute's own features, in its order, of those its map has, then the rest of its map.
static int32_t check_target_feature_required(const CheckInputAttribute* version, PredefinesFeatures callee, PredefinesFeatures caller)
{
    // The first the caller lacks of the whole map, where the attribute's own lacks none; the usual call lacks none at all,
    // which needs no reading of the attribute.
    const int32_t first = check_target_feature_lacked(callee, caller, 0);
    int32_t result = -1;
    const char* text = version->text.pointer;
    const uint32_t length = (uint32_t)version->text.length;
    uint32_t cursor = 0;
    const char* item = 0;
    uint32_t item_length = 0;
    while (first >= 0 && result < 0 && predefines_attribute_item(text, length, &cursor, &item, &item_length))
    {
        const bool named = !predefines_item_starts(item, item_length, "arch=") && !predefines_item_starts(item, item_length, "tune=") &&
                           !predefines_item_starts(item, item_length, "no-");
        const int32_t feature = named ? predefines_feature_index(item, item_length) : -1;
        result = feature >= 0 && predefines_has(callee, (uint32_t)feature) && !predefines_has(caller, (uint32_t)feature) ? feature : -1;
    }
    return result >= 0 ? result : first;
}

// Whether clang checks a call of a function of `callee`'s attributes in a function of `caller`'s (CodeGenFunction::EmitCall).
static bool check_target_feature_checked(const CheckTargetFeatureAttributes* caller, const CheckTargetFeatureAttributes* callee)
{
    return (callee->always_inline && (callee->targeted || caller->targeted)) || (caller->flatten && (caller->targeted || callee->targeted));
}

// Whether a call of a function of `callee`'s attributes, in a function of `caller`'s, may fail the check, wherever it is
// (the walk's filter): it is checked, and its callee's attribute applies and needs a feature the caller lacks, or the
// caller's does and lacks one of the callee's.
static bool check_target_feature_fails(CheckTargetFeatureMaps* maps, const CheckTargetFeatureAttributes* caller, PredefinesFeatures caller_map,
                                       const CheckTargetFeatureAttributes* callee)
{
    bool result = false;
    if (check_target_feature_checked(caller, callee) && callee->targeted && !callee->versions)
    {
        result = check_target_feature_required(callee->version, check_target_feature_map(maps, callee), caller_map) >= 0;
    }
    else if (check_target_feature_checked(caller, callee) && caller->targeted && !caller->versions)
    {
        result = check_target_feature_lacked(check_target_feature_map(maps, callee), caller_map, 0) >= 0;
    }
    return result;
}

// Appends an error to `output`'s run of them. Returns false without room.
static bool check_target_feature_report(Arena* output, CheckTargetFeatureError** errors, uint32_t* count, CheckTargetFeatureError error)
{
    CheckTargetFeatureError* slot = BUSTER_ARENA_PUSH_ARRAY(output, CheckTargetFeatureError, 1);
    if (slot != 0)
    {
        *slot = error;
        *errors = *count == 0 ? slot : *errors;
        *count += 1;
    }
    return slot != 0;
}

// The direct callee's object of call `node`, or 0: a function's name alone, or an overload's selection.
static uint32_t check_target_feature_callee(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t callee = graph->operand0[node];
    const uint32_t overload = check_program_builtin_index(stream->overloads, stream->overload_count, sizeof(CheckOverloadEffect), node);
    const CheckValue value = callee < graph->count ? stream->values[callee] : (CheckValue){ 0 };
    uint32_t object = overload != BUSTER_CHECK_NONE ? stream->overloads[overload].selected.object : 0;
    if (overload == BUSTER_CHECK_NONE && callee < graph->count && graph->ops[callee] == CHECK_OP_VALUE && value.category == CHECK_FUNCTION)
    {
        object = value.object;
    }
    return object < program->object_count ? object : 0;
}

// Checks body `b`'s calls, which its walk reached (`calls`, `count` keys of a call's node in the low half, in clang's
// emission order): appends each error. Returns false without room.
static bool check_target_feature_calls(CheckProgram* program, CheckTargetFeatureMaps* maps, uint32_t b, bool deferred,
                                       const CheckTargetFeatureAttributes* caller, PredefinesFeatures caller_map, const uint64_t* calls,
                                       uint32_t count, Arena* output, CheckTargetFeatureError** errors, uint32_t* error_count)
{
    const CheckGraph* graph = &program->input->graph;
    bool valid = true;
    for (uint32_t c = 0; valid && c < count; c += 1)
    {
        const uint32_t node = (uint32_t)calls[c];
        const uint32_t object = check_target_feature_callee(program, node);
        const uint32_t token = graph->tokens[graph->operand0[node]];
        const CheckTargetFeatureAttributes callee = object != 0 ? check_target_feature_attributes(program, object, token) : (CheckTargetFeatureAttributes){ 0 };
        const CheckTargetFeatureError base = {
            .token = token,
            .caller = program->bodies[b].object,
            .callee = object,
            .flatten = !callee.always_inline,
            .deferred = deferred,
        };
        if (object != 0 && check_target_feature_checked(caller, &callee) && callee.targeted && !callee.versions)
        {
            const int32_t feature = check_target_feature_required(callee.version, check_target_feature_map(maps, &callee), caller_map);
            CheckTargetFeatureError error = base;
            error.feature = (uint32_t)feature;
            valid = feature < 0 || check_target_feature_report(output, errors, error_count, error);
        }
        else if (object != 0 && check_target_feature_checked(caller, &callee) && caller->targeted && !caller->versions)
        {
            const PredefinesFeatures callee_map = check_target_feature_map(maps, &callee);
            for (int32_t feature = check_target_feature_lacked(callee_map, caller_map, 0); valid && feature >= 0;
                 feature = check_target_feature_lacked(callee_map, caller_map, (uint32_t)feature + 1))
            {
                CheckTargetFeatureError error = base;
                error.feature = (uint32_t)feature;
                valid = check_target_feature_report(output, errors, error_count, error);
            }
        }
    }
    return valid;
}

// Code generation's check of target features of `program`'s bodies (-c's selection): *errors, `*count` of them in
// `output`, as clang reports them: the first function's to have any of those it emits where the unit defines them
// (CheckProgram.body_eager), or else every deferred one's. Returns false without room in either arena.
static __attribute__((unused)) bool check_program_target_features(CheckProgram* program, const PredefinesOptions* options, bool scalar, Arena* output,
                                                                  Arena* scratch, CheckTargetFeatureError** errors, uint32_t* count)
{
    *errors = 0;
    *count = 0;
    bool valid = true;
    if (program->attribute_keys != 0 && options != NULL)
    {
        const CheckInput* input = program->input;
        const uint64_t mark = scratch->length;
        CheckTargetFeatureMaps maps = { .options = options, .host = program->host_read ? program->host : predefines_host_of(options) };
        maps.unit = predefines_apply(options, &maps.host, maps.host.cpu, 0, 0);
        // The objects with a key: a callee without one has no attribute, which only a flatten caller checks.
        const uint64_t words = ((uint64_t)program->object_count + 63) / 64;
        uint64_t* attributed = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, words);
        valid = attributed != 0;
        if (valid)
        {
            // The keys and their counts in locals: the stores into the bitset could otherwise be the program's fields.
            const uint64_t* keys = program->attribute_keys;
            const uint32_t key_count = program->attribute_key_count;
            const uint32_t object_count = program->object_count;
            memset(attributed, 0, words * sizeof(uint64_t));
            for (uint32_t k = 0; k < key_count; k += 1)
            {
                const uint32_t object = (uint32_t)(keys[k] >> 32);
                attributed[object / 64] |= object < object_count ? 1ull << (object % 64) : 0;
            }
        }
        const uint64_t items = (uint64_t)input->graph.count + program->control_count;
        CheckX86DiagnosticWalk walk = { .program = program, .calls = scratch };
        // Clang's emission order: the functions it must emit, where the unit defines them (body_eager), the first with an
        // error the last (it emits no function after it, nor the deferred ones); then, where none has one, every deferred
        // function (its deferred emission goes on past an error).
        for (uint32_t deferred = 0; valid && *count == 0 && deferred < 2; deferred += 1)
        {
            for (uint32_t b = 0; valid && (deferred != 0 || *count == 0) && b < program->body_count; b += 1)
            {
                const CheckProgramBody* body = program->bodies + b;
                const bool eager = program->body_eager != 0 && program->body_eager[b] != 0;
                bool candidate = false;
                CheckTargetFeatureAttributes caller = { 0 };
                PredefinesFeatures caller_map = maps.unit;
                if (eager == (deferred == 0))
                {
                    const bool keyed_caller = body->object < program->object_count && ((attributed[body->object / 64] >> (body->object % 64)) & 1) != 0;
                    caller = keyed_caller ? check_target_feature_attributes(program, body->object, body->declaration + 1) : (CheckTargetFeatureAttributes){ 0 };
                    caller_map = check_target_feature_map(&maps, &caller);
                    // A callee without an attribute of its own fails a flatten caller whose attribute lacks a unit's feature.
                    candidate = caller.flatten && caller.targeted && !caller.versions && check_target_feature_lacked(maps.unit, caller_map, 0) >= 0;
                    const bool plain = !caller.targeted && !caller.flatten;
                    for (uint32_t r = 0; !candidate && r < body->reference_count; r += 1)
                    {
                        const uint32_t object = program->references[body->reference_first + r];
                        const bool keyed = object < program->object_count && ((attributed[object / 64] >> (object % 64)) & 1) != 0;
                        CheckTargetFeatureCallee* answer = maps.callees + object % BUSTER_CHECK_TARGET_FEATURE_CALLEES;
                        if (keyed && plain && answer->object == object)
                        {
                            candidate = answer->fails;
                        }
                        else
                        {
                            const CheckTargetFeatureAttributes callee = keyed ? check_target_feature_attributes(program, object, UINT32_MAX) :
                                                                                (CheckTargetFeatureAttributes){ 0 };
                            candidate = check_target_feature_fails(&maps, &caller, caller_map, &callee);
                            answer->object = keyed && plain ? object : answer->object;
                            answer->fails = keyed && plain ? candidate : answer->fails;
                        }
                    }
                }
                if (candidate && walk.seen == 0)
                {
                    walk.seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, items);
                    walk.stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, items);
                    valid = walk.seen != 0 && walk.stack != 0;
                    if (valid)
                    {
                        memset(walk.seen, 0, items);
                    }
                }
                if (valid && candidate)
                {
                    // The body's calls, as the walk reaches them, into a run of scratch, each with its statement: the control,
                    // one more than its index, whose expression holds it, which a statement expression's own controls take
                    // from the expression that reaches them (they follow the body's, but clang emits them where they are).
                    const uint64_t calls_mark = scratch->length;
                    const uint64_t* calls = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 0);
                    walk.body = body;
                    walk.call_count = 0;
                    walk.call_room = calls != 0;
                    walk.tag = 0;
                    check_x86_diagnostic_push(&walk, 0, true);
                    while (walk.depth != 0 && program->status == CHECK_PROGRAM_OK)
                    {
                        const uint64_t entry = walk.stack[--walk.depth];
                        const uint64_t item = entry & UINT32_MAX;
                        const uint32_t control = item >= input->graph.count ? (uint32_t)(item - input->graph.count) - body->first : UINT32_MAX;
                        walk.tag = control < body->entry_count ? control + 1 : (uint32_t)(entry >> 32);
                        if (item < input->graph.count)
                        {
                            check_x86_diagnostic_expression(&walk, (uint32_t)item);
                        }
                        else
                        {
                            check_x86_diagnostic_control(&walk, control);
                        }
                    }
                    walk.depth = 0;
                    walk.tag = 0;
                    valid = walk.call_room && program->status == CHECK_PROGRAM_OK;
                    // The calls in clang's emission order: by their statements, whose controls are in the lowering's order,
                    // as clang emits them (a for loop's increment after its body), and in one statement by their ends (a
                    // call's node is its closing parenthesis), so that arguments' calls come before their call's. Two stable
                    // passes of the radix kernel, by end then by statement, of keys of the sort's key in the high half and
                    // the call's place in `calls` in the low; then the calls' nodes in the low half, in that order.
                    const uint32_t call_count = walk.call_count;
                    uint64_t* keys = valid && call_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, call_count) : 0;
                    uint64_t* temporary = keys != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, call_count) : 0;
                    valid = valid && (call_count == 0 || temporary != 0);
                    uint64_t* sorted = 0;
                    if (valid && call_count != 0)
                    {
                        uint32_t varying = 0;
                        for (uint32_t c = 0; c < call_count; c += 1)
                        {
                            const uint32_t end = input->graph.tokens[(uint32_t)calls[c]];
                            keys[c] = ((uint64_t)end << 32) | c;
                            varying |= end ^ input->graph.tokens[(uint32_t)calls[0]];
                        }
                        uint64_t* by_end = c_bindings_sort(keys, temporary, call_count, !scalar, varying);
                        uint64_t* other = by_end == keys ? temporary : keys;
                        varying = 0;
                        for (uint32_t j = 0; j < call_count; j += 1)
                        {
                            const uint32_t statement = (uint32_t)(calls[(uint32_t)by_end[j]] >> 32);
                            other[j] = ((uint64_t)statement << 32) | (uint32_t)by_end[j];
                            varying |= statement ^ (uint32_t)(calls[(uint32_t)by_end[0]] >> 32);
                        }
                        sorted = c_bindings_sort(other, by_end, call_count, !scalar, varying);
                        for (uint32_t j = 0; j < call_count; j += 1)
                        {
                            sorted[j] = (uint32_t)calls[(uint32_t)sorted[j]];
                        }
                    }
                    valid = valid &&
                            check_target_feature_calls(program, &maps, b, deferred != 0, &caller, caller_map, sorted, call_count, output, errors, count);
                    arena_pop_to(scratch, calls_mark);
                }
            }
        }
        arena_pop_to(scratch, mark);
    }
    return valid;
}
