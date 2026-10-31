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
// And a call of an x86 builtin whose required features (clang's expression of them, BuiltinIntrinsicInfo.features)
// the function's don't satisfy is one (#148, err_builtin_needs_feature), whatever either function's attributes, where
// clang emits the call (check_target_feature_emitted); before it, SSE's compares of a predicate past 7 in a function
// without AVX (check_target_feature_predicate). Clang checks a builtin's call before it emits its arguments, so its
// errors come before its arguments' calls': it is ordered by its beginning, a function's call by its end.
//
// A function's features are predefines_apply's for its attribute, or the unit's without one (ASTContext::
// getFunctionFeatureMap), and its attributes those of its declarations before the call (the callee's) or up to its
// definition (the caller's), as a declaration's inherited attributes are. The calls are those clang emits: the walk
// of #487's diagnostics (c_check_x86_diagnostic.c) reaches what a constant condition leaves running, not a dead
// branch's or operand's, sizeof's or what follows a return; a callee is direct where it is the function's name alone,
// not `(*f)` or `(&f)` (CodeGenFunction::EmitCallee). A cleanup attribute's call is one too
// (CallCleanupFunction::Emit, #147's CHECK_CONTROL_CLEANUP): of the function its argument names, with that function's
// attributes from its declarations before the argument, reported at the attribute (its first token:
// [[gnu::cleanup(f)]]'s scope's), and once for each variable whose scope an exit the walk reaches leaves: at the
// scope's end, where clang pops the cleanup and emits its call, after the scope's statements, the innermost variable's
// first, whichever exits reach it. Clang's errors are unrecoverable: it emits the functions it must
// emit where the unit defines them (ASTContext::DeclMustBeEmitted: an external definition, by GNU's and C99's rules of
// inline functions, or one with a used, constructor or destructor attribute, both by the declarations up to the
// definition, as the parser has them when it hands the definition over), and none after the first that has one, nor
// the functions it defers to the unit's end; where none of those has one, it emits every deferred function, past their
// errors. A declaration after a definition is no emission point: one that makes an inline definition external (C99's
// `extern int f(int);`) only queues it for the unit's end, so the first ordinary function with an error, before that
// declaration or after it, is the one reported, and the queued function's errors go with the deferred ones. So this
// reports the first such function's errors alone, or else every deferred function's, by the selection's
// CheckProgram.body_eager, each function's in clang's emission order: by statement, in the lowering's order of them (a
// for loop's increment after its body), and in a statement by the calls' ends (an argument's call before its own; but
// where clang evaluates out of source order, an assignment's right side first). Clang emits the eager functions in
// source order, and the deferred ones in the order its emission first referred to them or a declaration queued them,
// depth first; this takes those in source order. Where the feature an error names is the first of the rest of a
// function's map (an attribute's arch= CPU's, or a caller's attribute that turns several off), clang's order is its
// StringMap's and this one predefines_features', so the two may name another feature for the same call.
//
// The walk is code generation's emission: #487's walk, Sema's reachability, with code generation's rules where they
// differ (CheckTargetFeatureEmission): nothing after an expression statement that ends in a call that doesn't return,
// loops' blocks emitted whatever their conditions, and code after a label whatever precedes it. It still differs where
// clang emits a statement that holds a label from its start (an if, loop or switch statement after a point where its
// code stops, or the arm of an if or a switch that a constant condition would leave out), which it walks from the label
// on; and a call that doesn't return through a pointer, or of one of the C library's functions that clang knows as such
// (exit, abort) declared without the attribute, goes on (docs/x86-target.md).
//
// The pass is cold and scalar: it runs in a unit with a target attribute (CheckProgram.attribute_keys) or an x86
// builtin's call, and walks only a body that refers to a callee that may fail or calls a builtin that fails, whose call
// sites it then finds (clang's headers give every intrinsic one, but a unit compiled for the features its intrinsics
// need, the usual one, refers to none that fails).

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
    // An x86 builtin's call whose required features the caller lacks (#148, err_builtin_needs_feature: the builtin's
    // name and clang's expression of its features), or null for a call of a function.
    const BuiltinIntrinsicInfo* builtin;
    // The builtin's error is an SSE compare's predicate past 7 (check_target_feature_predicate), whose expression is avx.
    bool predicate;
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
    // A call of it doesn't return: code generation emits nothing after the statement it ends (CheckTargetFeatureEmission).
    bool noreturn;
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

// The x86 builtins' required features (#148): clang's expression of each builtin's (BuiltinIntrinsicInfo.features),
// evaluated against a function's map as checkTargetFeatures evaluates it (Builtin::evaluateRequiredTargetFeatures), by a
// table of predefines_features' names whose availabilities are the map's, made again when the map changes, and the
// answers for that map by the expression's address: a unit's builtins share a few expressions, and its functions a few
// maps.
#define BUSTER_CHECK_TARGET_FEATURE_ANSWERS 64

typedef struct CheckTargetFeatureAnswer CheckTargetFeatureAnswer;
struct CheckTargetFeatureAnswer
{
    const char* features;
    bool fails;
};

typedef struct CheckTargetFeatureBuiltins CheckTargetFeatureBuiltins;
struct CheckTargetFeatureBuiltins
{
    CBuiltinIntrinsicFeature table[BUSTER_ARRAY_LENGTH(predefines_features)];
    PredefinesFeatures map;
    bool ready;
    CheckTargetFeatureAnswer answers[BUSTER_CHECK_TARGET_FEATURE_ANSWERS];
};

// The functions' feature maps: the unit's, and those of the attribute strings resolved so far; the filter's answers; and
// the builtins' table.
typedef struct CheckTargetFeatureMaps CheckTargetFeatureMaps;
struct CheckTargetFeatureMaps
{
    const PredefinesOptions* options;
    PredefinesHost host;
    PredefinesFeatures unit;
    CheckTargetFeatureCached cache[BUSTER_CHECK_TARGET_FEATURE_CACHE];
    CheckTargetFeatureCallee callees[BUSTER_CHECK_TARGET_FEATURE_CALLEES];
    CheckTargetFeatureBuiltins builtins;
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
                result.noreturn |= attribute->kind == CHECK_ATTRIBUTE_NORETURN;
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

// Whether builtin expression `features` (`length` bytes, clang's grammar) fails in a function of features `map`: any
// answer but available, as clang's map has no other names than predefines_features', which test-checker checks of every
// x86 builtin's expression (check_test_target_feature_builtins), as it checks the table's names.
static bool check_target_feature_expression_fails(CheckTargetFeatureBuiltins* builtins, PredefinesFeatures map, const char* features, uint32_t length)
{
    if (!builtins->ready || memcmp(&builtins->map, &map, sizeof(map)) != 0)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(predefines_features); i += 1)
        {
            builtins->table[i] = (CBuiltinIntrinsicFeature){
                .name = predefines_features[i].name,
                .name_length = (uint32_t)strlen(predefines_features[i].name),
                .available = predefines_has(map, i),
            };
        }
        builtins->map = map;
        builtins->ready = true;
        memset(builtins->answers, 0, sizeof(builtins->answers));
    }
    CheckTargetFeatureAnswer* answer = builtins->answers + ((uintptr_t)features >> 3) % BUSTER_CHECK_TARGET_FEATURE_ANSWERS;
    if (answer->features != features)
    {
        CBuiltinIntrinsicFeatureFrame frames[8];
        const CBuiltinIntrinsicFeatureStatus status =
            c_builtin_intrinsic_features_evaluate_valid(features, length, builtins->table, BUSTER_ARRAY_LENGTH(builtins->table), frames, BUSTER_ARRAY_LENGTH(frames));
        answer->features = features;
        answer->fails = status != CBUILTIN_INTRINSIC_FEATURE_AVAILABLE;
    }
    return answer->fails;
}

// The x86 builtin of call `node` (CHECK_OP_BUILTIN_X86) where clang emits the call, or null. Clang checks a builtin's
// features where it emits the call (CodeGenFunction::EmitBuiltinExpr's checkTargetFeatures), which it doesn't where it
// folds the call first: a builtin it may evaluate (its metadata's E attribute) whose result is an integer or a floating
// value and whose arguments are constants (EvaluateAsRValue, whose result is then an integer's or a floating value's),
// or a call in a constant context (CheckX86Call.constant_evaluated_context), which it evaluates and never emits. Where
// clang folds such a call and the checker doesn't, the lowering refuses it by name.
static const BuiltinIntrinsicInfo* check_target_feature_emitted(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t effect = check_program_builtin_index(stream->x86, stream->x86_count, sizeof(CheckBuiltinX86Effect), node);
    const uint32_t call = check_program_builtin_index(graph->x86_calls, graph->x86_count, sizeof(CheckX86Call), node);
    const BuiltinIntrinsicInfo* info = effect != BUSTER_CHECK_NONE ? stream->x86[effect].info : NULL;
    const uint8_t kind = info != NULL ? check_action_kind(program->types, stream->values[node].type) : BUSTER_TYPE_KIND_NONE;
    bool folded = info != NULL && info->constant_evaluable_attribute && (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING);
    for (uint32_t i = 0; folded && i < graph->argument_counts[node]; i += 1)
    {
        const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
        const CheckConstantKind constant = argument < graph->count ? stream->values[argument].constant_kind : CHECK_CONSTANT_NONE;
        folded = constant == CHECK_CONSTANT_INTEGER || constant == CHECK_CONSTANT_FLOATING;
    }
    const bool constant = call != BUSTER_CHECK_NONE && graph->x86_calls[call].constant_evaluated_context;
    return info != NULL && !folded && !constant ? info : NULL;
}

// The x86 builtin of call `node` whose required features a function of features `map` lacks where clang emits the call
// (check_target_feature_emitted), or null.
static const BuiltinIntrinsicInfo* check_target_feature_builtin(const CheckProgram* program, CheckTargetFeatureMaps* maps, PredefinesFeatures map, uint32_t node)
{
    const BuiltinIntrinsicInfo* info = check_target_feature_emitted(program, node);
    const bool fails = info != NULL && check_target_feature_expression_fails(&maps->builtins, map, info->features, info->features_length);
    return fails ? info : NULL;
}

// SSE's compares, __builtin_ia32_cmpps, cmpss, cmppd and cmpsd, take AVX's 32 predicates too, and clang's code
// generation checks one past SSE's 8 before the builtin's own features, where it emits the call: in a function without
// AVX, "needs target feature avx" (CodeGenFunction::checkTargetFeatures, at the call's beginning). The builtin of call
// `node` where it is such a compare of a predicate past 7 in a function of features `map` without AVX, or null; Sema has
// checked that the predicate is a constant from 0 to 31.
static const BuiltinIntrinsicInfo* check_target_feature_predicate(const CheckProgram* program, PredefinesFeatures map, uint32_t node)
{
    static const char* const compares[] = { "__builtin_ia32_cmpps", "__builtin_ia32_cmpss", "__builtin_ia32_cmppd", "__builtin_ia32_cmpsd" };
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const BuiltinIntrinsicInfo* info = check_target_feature_emitted(program, node);
    bool compare = false;
    for (uint32_t i = 0; info != NULL && i < BUSTER_ARRAY_LENGTH(compares); i += 1)
    {
        compare |= info->name_length == strlen(compares[i]) && memcmp(info->name, compares[i], info->name_length) == 0;
    }
    const uint32_t predicate = compare && graph->argument_counts[node] == 3 ? graph->argument_nodes[graph->argument_offsets[node] + 2] : BUSTER_CHECK_NONE;
    const bool past = predicate < graph->count && stream->values[predicate].constant_kind == CHECK_CONSTANT_INTEGER && stream->values[predicate].integer > 7;
    const int32_t avx = past ? predefines_feature_index("avx", 3) : -1;
    return avx >= 0 && !predefines_has(map, (uint32_t)avx) ? info : NULL;
}

// Where clang reports x86 builtin call `node`'s error: the call's beginning, its name (CheckInputIntrinsicCall.keyword).
static uint32_t check_target_feature_builtin_token(const CheckInput* input, uint32_t node)
{
    const uint32_t closing = input->graph.tokens[node];
    const uint32_t id = input->intrinsic_ids != NULL && closing < input->token_count ? input->intrinsic_ids[closing] : 0;
    return id != 0 && id <= input->intrinsic_call_count ? input->intrinsic_calls[id - 1].keyword : closing;
}

// Where clang reports cleanup `record`'s call (CallCleanupFunction::Emit, at its attribute's location): the applied
// attribute's first token, the scope's of [[gnu::cleanup(f)]], or else its argument.
static uint32_t check_target_feature_cleanup_token(const CheckInput* input, const CheckInputCleanup* record)
{
    const CheckInputAttributes* attributes = record->declaration < input->token_count ? input->attributes[record->declaration] : 0;
    uint32_t result = record->argument;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool applied = attribute->kind == CHECK_ATTRIBUTE_CLEANUP && !attribute->ignored && attribute->argument_first == record->argument;
        result = applied ? attribute->first : result;
    }
    return result;
}

// Code generation's emission (CodeGenFunction::EmitStmt and the emitters it calls), where it differs from #487's walk,
// Sema's reachability, which the check's walk follows otherwise (check_target_feature_control):
//
// - An expression statement whose code ends in a block that nothing branches to ends what code generation emits, until a
//   block it always emits: a call of a function that doesn't return leaves such a block (EmitCall's dummy block), and so
//   does __builtin_unreachable() ("unreachable.cont"), and EmitStmt erases it where only straight-line code followed the
//   call in the statement. A join after the call (&&, || or ?: whose condition isn't a constant, but a ?: of two
//   constants, a select; va_arg; a statement expression with statements before its value) has the call's block as a
//   predecessor, and the statement ends where code goes on; so does a declaration's initializer, which EmitStmt doesn't
//   erase (check_target_feature_dead_end).
// - A while or for statement's body is emitted whatever its condition: `while (0)`'s too. Its end is emitted where a
//   branch reaches it: a for statement's condition always branches there, a constant one too; a while statement's
//   doesn't where it is a constant that is true. A do statement's condition and end are emitted whatever its body's
//   code leaves, and so is a for statement's increment, once their statement's code begins (the extra edges,
//   check_target_feature_emission_prepare).
// - A label begins a block whatever code precedes it: each of the body's source labels outside statement expressions is
//   a root of the walk (CheckProgram.label_positions).
typedef struct CheckTargetFeatureEmission CheckTargetFeatureEmission;
struct CheckTargetFeatureEmission
{
    // The dead-end analysis's bits per node, and its stack of `capacity` items.
    uint8_t* bits;
    uint64_t* stack;
    uint64_t capacity;
    // The body's extra edges: per control, the first control that its statement's code always emits beside it, and per
    // such control, the next of the same control's, BUSTER_CHECK_NONE ending each list.
    uint32_t* first;
    uint32_t* next;
};

#define BUSTER_CHECK_TARGET_FEATURE_DEAD 1u
#define BUSTER_CHECK_TARGET_FEATURE_STRAIGHT 2u

// Whether ?: `node` is a select, which branches nowhere (ScalarExprEmitter's isCheapEnoughToEvaluateUnconditionally):
// both its arms constants, of a scalar type or void.
static bool check_target_feature_select(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckValue* values = program->stream->values;
    const uint32_t flags = BUSTER_CHECK_ICE | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_GNU_CONSTANT;
    const uint32_t arms[] = { graph->operand1[node], graph->operand2[node] };
    bool constant = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(arms); i += 1)
    {
        constant = constant && arms[i] < graph->count && (values[arms[i]].flags & flags) != 0 && (values[arms[i]].flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
    }
    const uint32_t kind = type_kind(program->types, type_strip_aligned(program->types, values[node].type));
    return constant && kind != BUSTER_TYPE_KIND_STRUCT && kind != BUSTER_TYPE_KIND_UNION && kind != BUSTER_TYPE_KIND_COMPLEX;
}

// Whether code generation's code of `node` joins branches, whatever precedes it (its operands then don't decide where
// its statement ends): &&, || and ?: whose condition isn't a constant, but a ?: that is a select, va_arg (the System V
// ABI's register and stack paths), and a statement expression with statements before its value.
static bool check_target_feature_join(CheckProgram* program, uint32_t node)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const CheckOp op = graph->ops[node];
    bool truth = false;
    const bool decided = op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL;
    const bool select = op == CHECK_OP_CONDITIONAL && check_target_feature_select(program, node);
    const uint32_t region = op == CHECK_OP_STATEMENT_VALUE ? input->expression_regions[graph->tokens[node]] : 0;
    const bool statements = region != 0 && (region > program->region_count || program->regions[region - 1].count != 1);
    return (decided && !select && !check_program_runtime_truth(program, node, graph->operand0[node], &truth)) || op == CHECK_OP_VARARGS || statements;
}

// The `k`th operand of `node` that code generation's code evaluates, in its order, or BUSTER_CHECK_NONE: an assignment's
// right side before its left, a call's callee before its arguments, the operand a constant condition selects alone, and
// none of a join's (check_target_feature_join), nor of an operand that isn't evaluated (sizeof's, a builtin's query).
static uint32_t check_target_feature_operand(CheckProgram* program, uint32_t node, uint32_t k)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const CheckOp op = graph->ops[node];
    uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    bool truth = false;
    const bool assignment = op == CHECK_OP_ASSIGN || (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN);
    const bool decided = op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL;
    uint32_t result = BUSTER_CHECK_NONE;
    if (op == CHECK_OP_VALUE || op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF || op == CHECK_OP_BUILTIN_QUERY || check_target_feature_join(program, node))
    {
        result = BUSTER_CHECK_NONE;
    }
    else if (op == CHECK_OP_STATEMENT_VALUE)
    {
        const uint32_t region = input->expression_regions[graph->tokens[node]];
        result = k == 0 && region != 0 && region <= program->region_count ? program->regions[region - 1].result : BUSTER_CHECK_NONE;
    }
    else if (decided && check_program_runtime_truth(program, node, operands[0], &truth))
    {
        // `1 && x` and `0 || x` emit x alone, `0 && x` and `1 || x` nothing; `c ? a : b` the arm c selects.
        const uint32_t live = op == CHECK_OP_CONDITIONAL ? operands[truth ? 1 : 2] : truth == (op == CHECK_OP_LOGICAL_AND) ? operands[1] : BUSTER_CHECK_NONE;
        result = k == 0 ? live : BUSTER_CHECK_NONE;
    }
    else
    {
        if (assignment)
        {
            operands[0] = graph->operand1[node];
            operands[1] = graph->operand0[node];
        }
        uint32_t seen = 0;
        for (uint32_t i = 0; result == BUSTER_CHECK_NONE && i < BUSTER_ARRAY_LENGTH(operands); i += 1)
        {
            result = operands[i] < graph->count && seen++ == k ? operands[i] : BUSTER_CHECK_NONE;
        }
        const uint32_t mask = result == BUSTER_CHECK_NONE && check_graph_has_arguments(op) ? check_program_argument_mask(program, node) : 0;
        for (uint32_t i = 0; result == BUSTER_CHECK_NONE && check_graph_has_arguments(op) && i < graph->argument_counts[node]; i += 1)
        {
            const bool evaluated = op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86 || (i < 32 && (mask & (1u << i)) != 0);
            const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
            result = evaluated && argument < graph->count && seen++ == k ? argument : BUSTER_CHECK_NONE;
        }
    }
    return result;
}

// Whether code generation's code of the expression statement `root` ends in a block that nothing branches to
// (CheckTargetFeatureEmission), by a postorder walk of the operands it evaluates: a node's code is straight-line where
// none of its own or its operands' joins branches, and ends in such a block where it calls a function that doesn't
// return, by the callee's declarations before the call, or __builtin_unreachable(), or where an operand's code does and
// only straight-line code follows. Without room on its stack, false: the statement goes on.
static bool check_target_feature_dead_end(CheckProgram* program, CheckTargetFeatureEmission* emission, uint32_t root)
{
    const CheckGraph* graph = &program->input->graph;
    uint64_t depth = 0;
    bool room = root < graph->count && emission->capacity != 0;
    if (room)
    {
        emission->stack[depth++] = root;
    }
    while (room && depth != 0)
    {
        const uint64_t item = emission->stack[--depth];
        const uint32_t node = (uint32_t)item;
        uint32_t operand = 0;
        if ((item >> 32) == 0)
        {
            // The node's turn after its operands', which go on the stack above it.
            emission->stack[depth++] = ((uint64_t)1 << 32) | node;
            for (uint32_t k = 0; room && (operand = check_target_feature_operand(program, node, k)) != BUSTER_CHECK_NONE; k += 1)
            {
                room = depth < emission->capacity;
                if (room)
                {
                    emission->stack[depth++] = operand;
                }
            }
        }
        else
        {
            // Its operands' code in order, each ending in such a block, or after one with only straight-line code; then
            // its own: a join's ends where code goes on.
            const bool join = check_target_feature_join(program, node);
            bool dead = false;
            bool straight = !join;
            for (uint32_t k = 0; (operand = check_target_feature_operand(program, node, k)) != BUSTER_CHECK_NONE; k += 1)
            {
                const uint8_t bits = emission->bits[operand];
                dead = (bits & BUSTER_CHECK_TARGET_FEATURE_DEAD) != 0 || (dead && (bits & BUSTER_CHECK_TARGET_FEATURE_STRAIGHT) != 0);
                straight = straight && (bits & BUSTER_CHECK_TARGET_FEATURE_STRAIGHT) != 0;
            }
            const CheckOp op = graph->ops[node];
            const uint32_t callee = op == CHECK_OP_CALL ? check_target_feature_callee(program, node) : 0;
            const uint32_t token = op == CHECK_OP_CALL && graph->operand0[node] < graph->count ? graph->tokens[graph->operand0[node]] : 0;
            const bool noreturn = callee != 0 && check_target_feature_attributes(program, callee, token).noreturn;
            const bool unreachable = op == CHECK_OP_BUILTIN_MISC && graph->members[node] == CBUILTIN_UNREACHABLE;
            dead = !join && (dead || noreturn || unreachable);
            emission->bits[node] = (uint8_t)((dead ? BUSTER_CHECK_TARGET_FEATURE_DEAD : 0) | (straight ? BUSTER_CHECK_TARGET_FEATURE_STRAIGHT : 0));
        }
    }
    return room && (emission->bits[root] & BUSTER_CHECK_TARGET_FEATURE_DEAD) != 0;
}

// The loop whose condition the body's BRANCH_FALSE control `index` is, by the back edge where it branches to: 1 for a
// while statement's (its condition's token on both), 2 for a for statement's (its closing parenthesis on the back edge,
// the condition's `;` one of its children), 0 for an if statement's.
static uint32_t check_target_feature_loop(const CheckProgram* program, const CheckProgramBody* body, uint32_t index)
{
    const CheckControl* controls = program->controls + body->first;
    const Parser* parser = program->parser;
    const CheckControl* entry = controls + index;
    const CheckControl* back = entry->target - 1 > index && entry->target - 1 < body->count ? controls + entry->target - 1 : 0;
    const bool loop = back != 0 && back->kind == CHECK_CONTROL_JUMP && back->target == index;
    const bool by_for = loop && back->token < parser->tokens.count && entry->token < parser->tokens.count &&
                        parser->tree.roles[back->token] == PARSE_ROLE_CLOSE_FOR && parser->tree.parents[entry->token] == back->token;
    return loop && back->token == entry->token ? 1 : by_for ? 2 : 0;
}

// The body's extra edges (CheckTargetFeatureEmission.first and next, of `count` controls each, in `scratch`), and its
// labels' roots, onto `walk`'s stack: a do statement's condition from its body's first control, which its condition
// branches back to, and a for statement's increment from its condition, or its body's first control without one,
// which its back edge goes to; and each source label outside statement expressions (CheckProgram.label_positions).
// Returns false without room.
static bool check_target_feature_emission_prepare(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, Arena* scratch)
{
    const CheckProgram* program = walk->program;
    const CheckProgramBody* body = walk->body;
    const CheckControl* controls = program->controls + body->first;
    const Parser* parser = program->parser;
    const uint32_t count = body->count;
    emission->first = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    emission->next = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    const bool valid = emission->first != 0 && emission->next != 0;
    if (valid)
    {
        memset(emission->first, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(emission->next, 0xff, (uint64_t)count * sizeof(uint32_t));
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const CheckControl* control = controls + i;
        const bool closes_for = control->token < parser->tokens.count && parser->tree.roles[control->token] == PARSE_ROLE_CLOSE_FOR;
        uint32_t extra = BUSTER_CHECK_NONE;
        if (control->kind == CHECK_CONTROL_BRANCH_TRUE && control->target < count)
        {
            extra = i;
        }
        else if (control->kind == CHECK_CONTROL_JUMP && control->target < i && closes_for && i != 0 && controls[i - 1].kind == CHECK_CONTROL_EVALUATE &&
                 controls[i - 1].token == control->token)
        {
            extra = i - 1;
            while (extra != 0 && controls[extra - 1].kind == CHECK_CONTROL_EVALUATE && controls[extra - 1].token == control->token)
            {
                extra -= 1;
            }
        }
        if (extra != BUSTER_CHECK_NONE)
        {
            emission->next[extra] = emission->first[control->target];
            emission->first[control->target] = extra;
        }
    }
    for (uint32_t l = 0; valid && l < body->label_count; l += 1)
    {
        const uint32_t position = program->label_positions[body->label_first + l];
        if (position < body->entry_count)
        {
            check_x86_diagnostic_push(walk, position, true);
        }
    }
    return valid;
}

// The check's walk's step at the body's control `index`: code generation's emission (CheckTargetFeatureEmission) where
// it differs from #487's walk, which takes the other controls.
static void check_target_feature_control(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, uint32_t index)
{
    CheckProgram* program = walk->program;
    const CheckControl* entry = program->controls + walk->body->first + index;
    for (uint32_t extra = emission->first[index]; extra != BUSTER_CHECK_NONE; extra = emission->next[extra])
    {
        check_x86_diagnostic_push(walk, extra, true);
    }
    const uint32_t loop = entry->kind == CHECK_CONTROL_BRANCH_FALSE ? check_target_feature_loop(program, walk->body, index) : 0;
    if (entry->kind == CHECK_CONTROL_EVALUATE)
    {
        check_x86_diagnostic_push(walk, entry->node, false);
        if (!check_target_feature_dead_end(program, emission, entry->node))
        {
            check_x86_diagnostic_push(walk, index + 1, true);
        }
    }
    else if (entry->kind == CHECK_CONTROL_BRANCH_TRUE || loop != 0)
    {
        // A do statement's condition, which its end follows whatever its value, or a while or for statement's, which its
        // body follows whatever its value; a constant one has no branch where it can't go: back from `do ... while (0)`,
        // out of `while (1)`.
        bool truth = false;
        const bool known = check_program_runtime_truth(program, entry->node, entry->node, &truth);
        const bool branch = entry->kind == CHECK_CONTROL_BRANCH_TRUE ? !known || truth : loop == 2 || !known || !truth;
        check_x86_diagnostic_push(walk, entry->node, false);
        if (branch)
        {
            check_x86_diagnostic_push(walk, entry->target, true);
        }
        check_x86_diagnostic_push(walk, index + 1, true);
    }
    else
    {
        check_x86_diagnostic_control(walk, index);
    }
}

// Checks body `b`'s calls, which its walk reached (`calls`, `count` of them in clang's emission order: a call's node in
// the low half, or with the high half 1, a cleanup's scope's end, its CHECK_CONTROL_CLEANUP's index in the body):
// appends each error. Returns false without room.
static bool check_target_feature_calls(CheckProgram* program, CheckTargetFeatureMaps* maps, uint32_t b, bool deferred,
                                       const CheckTargetFeatureAttributes* caller, PredefinesFeatures caller_map, const uint64_t* calls,
                                       uint32_t count, Arena* output, CheckTargetFeatureError** errors, uint32_t* error_count)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    bool valid = true;
    for (uint32_t c = 0; valid && c < count; c += 1)
    {
        const uint32_t node = (uint32_t)calls[c];
        const bool cleanup = (calls[c] >> 32) != 0;
        // An x86 builtin's call: its own features against the caller's (#148), whatever either function's attributes.
        const bool builtin = !cleanup && graph->ops[node] == CHECK_OP_BUILTIN_X86;
        // A cleanup's function, by its declarations before the attribute's argument, where the argument looks it up.
        const CheckInputCleanup* record = cleanup ? input->cleanups + program->controls[program->bodies[b].first + node].target : 0;
        const uint32_t function = cleanup && record->function < input->token_count ? input->objects[record->function] : 0;
        const uint32_t object = cleanup ? (function < program->object_count ? function : 0) : builtin ? 0 : check_target_feature_callee(program, node);
        const uint32_t token = cleanup ? check_target_feature_cleanup_token(input, record) :
                               builtin ? check_target_feature_builtin_token(input, node) : graph->tokens[graph->operand0[node]];
        const uint32_t limit = cleanup ? record->argument : token;
        const CheckTargetFeatureAttributes callee = object != 0 ? check_target_feature_attributes(program, object, limit) : (CheckTargetFeatureAttributes){ 0 };
        const CheckTargetFeatureError base = {
            .token = token,
            .caller = program->bodies[b].object,
            .callee = object,
            .flatten = !callee.always_inline,
            .deferred = deferred,
        };
        // An SSE compare's predicate past 7 first, then the builtin's own features, as clang checks them.
        const BuiltinIntrinsicInfo* predicate = builtin ? check_target_feature_predicate(program, caller_map, node) : NULL;
        if (predicate != NULL)
        {
            CheckTargetFeatureError error = base;
            error.builtin = predicate;
            error.predicate = true;
            valid = check_target_feature_report(output, errors, error_count, error);
        }
        const BuiltinIntrinsicInfo* lacked = builtin ? check_target_feature_builtin(program, maps, caller_map, node) : NULL;
        if (builtin)
        {
            CheckTargetFeatureError error = base;
            error.builtin = lacked;
            valid = valid && (lacked == NULL || check_target_feature_report(output, errors, error_count, error));
        }
        else if (object != 0 && check_target_feature_checked(caller, &callee) && callee.targeted && !callee.versions)
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
// (CheckProgram.body_eager, by the declarations up to each definition), or else every deferred one's. Returns false
// without room in either arena.
static __attribute__((unused)) bool check_program_target_features(CheckProgram* program, const PredefinesOptions* options, bool scalar, Arena* output,
                                                                  Arena* scratch, CheckTargetFeatureError** errors, uint32_t* count)
{
    *errors = 0;
    *count = 0;
    bool valid = true;
    // A unit with a target attribute (attribute_keys), or with an x86 builtin's call (#148), whose features its function
    // may lack.
    if ((program->attribute_keys != 0 || program->stream->x86_count != 0) && options != NULL)
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
        // The bodies with an x86 builtin's call whose features their functions' maps lack (#148), a byte each: each call's
        // body is the one its record names (CheckInputIntrinsicCall.function_body, the body's opening brace), and its
        // function's map its attributes', by the declarations up to its definition, as the walk's caller's below.
        uint8_t* lacking = valid && program->stream->x86_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->body_count + 1) : 0;
        valid = valid && (program->stream->x86_count == 0 || lacking != 0);
        if (lacking != 0)
        {
            memset(lacking, 0, (uint64_t)program->body_count + 1);
            uint32_t last = UINT32_MAX;
            PredefinesFeatures last_map = maps.unit;
            for (uint32_t i = 0; i < program->stream->x86_count; i += 1)
            {
                const uint32_t node = program->stream->x86[i].node;
                const uint32_t closing = node < input->graph.count ? input->graph.tokens[node] : UINT32_MAX;
                const uint32_t id = input->intrinsic_ids != NULL && closing < input->token_count ? input->intrinsic_ids[closing] : 0;
                const uint32_t opening = id != 0 && id <= input->intrinsic_call_count ? input->intrinsic_calls[id - 1].function_body : BUSTER_CHECK_NONE;
                // The bodies are in source order: the last whose opening brace isn't past the call's body's.
                uint32_t low = 0;
                uint32_t high = program->body_count;
                while (low < high)
                {
                    const uint32_t middle = low + (high - low) / 2;
                    low = program->bodies[middle].opening <= opening ? middle + 1 : low;
                    high = program->bodies[middle].opening <= opening ? high : middle;
                }
                const uint32_t b = low != 0 && program->bodies[low - 1].opening == opening ? low - 1 : UINT32_MAX;
                if (b != UINT32_MAX && b != last)
                {
                    const CheckProgramBody* body = program->bodies + b;
                    const bool keyed = body->object < program->object_count && ((attributed[body->object / 64] >> (body->object % 64)) & 1) != 0;
                    const CheckTargetFeatureAttributes caller =
                        keyed ? check_target_feature_attributes(program, body->object, body->declaration + 1) : (CheckTargetFeatureAttributes){ 0 };
                    last_map = check_target_feature_map(&maps, &caller);
                    last = b;
                }
                if (b != UINT32_MAX && lacking[b] == 0)
                {
                    lacking[b] = check_target_feature_builtin(program, &maps, last_map, node) != NULL ||
                                 check_target_feature_predicate(program, last_map, node) != NULL;
                }
            }
        }
        const uint64_t items = (uint64_t)input->graph.count + program->control_count;
        CheckX86DiagnosticWalk walk = { .program = program, .calls = scratch };
        CheckTargetFeatureEmission emission = { 0 };
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
                    // A callee without an attribute of its own fails a flatten caller whose attribute lacks a unit's feature, and
                    // an x86 builtin's call one whose features the caller lacks.
                    candidate = (caller.flatten && caller.targeted && !caller.versions && check_target_feature_lacked(maps.unit, caller_map, 0) >= 0) ||
                                (lacking != 0 && lacking[b] != 0);
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
                    walk.cleanup_tags = input->cleanup_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, input->cleanup_count) : 0;
                    // The dead-end analysis's: each node at most twice on its stack, before and after its operands.
                    emission.bits = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, input->graph.count);
                    emission.capacity = (uint64_t)input->graph.count * 2;
                    emission.stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, emission.capacity);
                    valid = walk.seen != 0 && walk.stack != 0 && (input->cleanup_count == 0 || walk.cleanup_tags != 0) && emission.bits != 0 &&
                            emission.stack != 0;
                    if (valid)
                    {
                        memset(walk.seen, 0, items);
                    }
                    if (valid && walk.cleanup_tags != 0)
                    {
                        memset(walk.cleanup_tags, 0, (uint64_t)input->cleanup_count * sizeof(uint32_t));
                    }
                }
                if (valid && candidate)
                {
                    // The body's calls, as the walk reaches them, into a run of scratch, each with its statement: the control,
                    // one more than its index, whose expression holds it, which a statement expression's own controls take
                    // from the expression that reaches them (they follow the body's, but clang emits them where they are).
                    // The walk is code generation's emission (CheckTargetFeatureEmission): its extra edges and its labels' roots
                    // first, then the run.
                    const uint64_t calls_mark = scratch->length;
                    walk.body = body;
                    walk.tag = 0;
                    check_x86_diagnostic_push(&walk, 0, true);
                    const bool prepared = check_target_feature_emission_prepare(&walk, &emission, scratch);
                    const uint64_t* calls = prepared ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 0) : 0;
                    walk.call_count = 0;
                    walk.call_room = calls != 0;
                    while (walk.call_room && walk.depth != 0 && program->status == CHECK_PROGRAM_OK)
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
                            check_target_feature_control(&walk, &emission, control);
                        }
                    }
                    walk.depth = 0;
                    walk.tag = 0;
                    valid = walk.call_room && program->status == CHECK_PROGRAM_OK;
                    // The cleanups' calls, each where clang emits it, once, whichever exits the walk reached it by: at its
                    // scope's end, the CLEANUP control at its scope's closing token, after the run's calls, the innermost
                    // variable's first, with that control's statement, or in a statement expression the one that reached it.
                    const uint32_t call_count = walk.call_count;
                    uint32_t item_count = call_count;
                    for (uint32_t i = 0; valid && walk.cleanup_count != 0 && i < body->count; i += 1)
                    {
                        const CheckControl* control = program->controls + body->first + i;
                        const uint32_t record = control->kind == CHECK_CONTROL_CLEANUP ? control->target : BUSTER_CHECK_NONE;
                        const uint32_t scope = record < input->cleanup_count ? input->cleanups[record].scope : BUSTER_CHECK_NONE;
                        if (scope < program->parser->tokens.count && walk.cleanup_tags[record] != 0 && control->token == program->parser->tree.parents[scope])
                        {
                            uint64_t* item = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 1);
                            valid = item != 0;
                            if (valid)
                            {
                                const uint32_t statement = i < body->entry_count ? i + 1 : walk.cleanup_tags[record];
                                *item = ((uint64_t)statement << 32) | i;
                                item_count += 1;
                            }
                            walk.cleanup_tags[record] = 0;
                            walk.cleanup_count -= 1;
                        }
                    }
                    // Every variable reached has its scope's end, as the controls are written.
                    valid = valid && walk.cleanup_count == 0;
                    // The calls in clang's emission order: by their statements, whose controls are in the lowering's order,
                    // as clang emits them (a for loop's increment after its body), and in one statement by their ends (a
                    // call's node is its closing parenthesis, a cleanup's its scope's closing token), so that arguments'
                    // calls come before their call's. Two stable passes of the radix kernel, by end then by statement, of
                    // keys of the sort's key in the high half and the item's place in `calls` in the low; then the calls'
                    // nodes in the low half, and the cleanups' controls with the high half 1, in that order.
                    uint64_t* keys = valid && item_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, item_count) : 0;
                    uint64_t* temporary = keys != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, item_count) : 0;
                    valid = valid && (item_count == 0 || temporary != 0);
                    uint64_t* sorted = 0;
                    if (valid && item_count != 0)
                    {
                        uint32_t varying = 0;
                        for (uint32_t c = 0; c < item_count; c += 1)
                        {
                            // A builtin's call by its beginning, not its end: clang checks its features before it emits its
                            // arguments (CodeGenFunction::EmitBuiltinExpr), so before their calls' errors.
                            const uint32_t value = (uint32_t)calls[c];
                            const bool builtin = c < call_count && input->graph.ops[value] == CHECK_OP_BUILTIN_X86;
                            const uint32_t end = builtin ? check_target_feature_builtin_token(input, value) :
                                                 c < call_count ? input->graph.tokens[value] : program->controls[body->first + value].token;
                            keys[c] = ((uint64_t)end << 32) | c;
                            varying |= end ^ (uint32_t)(keys[0] >> 32);
                        }
                        uint64_t* by_end = c_bindings_sort(keys, temporary, item_count, !scalar, varying);
                        uint64_t* other = by_end == keys ? temporary : keys;
                        varying = 0;
                        for (uint32_t j = 0; j < item_count; j += 1)
                        {
                            const uint32_t statement = (uint32_t)(calls[(uint32_t)by_end[j]] >> 32);
                            other[j] = ((uint64_t)statement << 32) | (uint32_t)by_end[j];
                            varying |= statement ^ (uint32_t)(calls[(uint32_t)by_end[0]] >> 32);
                        }
                        sorted = c_bindings_sort(other, by_end, item_count, !scalar, varying);
                        for (uint32_t j = 0; j < item_count; j += 1)
                        {
                            const uint32_t place = (uint32_t)sorted[j];
                            sorted[j] = ((uint64_t)(place >= call_count) << 32) | (uint32_t)calls[place];
                        }
                    }
                    valid = valid &&
                            check_target_feature_calls(program, &maps, b, deferred != 0, &caller, caller_map, sorted, item_count, output, errors, count);
                    arena_pop_to(scratch, calls_mark);
                }
            }
        }
        arena_pop_to(scratch, mark);
    }
    return valid;
}
