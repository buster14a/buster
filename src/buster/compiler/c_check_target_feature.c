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
// branch's or operand's — an operand a label can enter aside — sizeof's or what follows a return; a callee is direct
// where it is the function's name alone, not `(*f)` or `(&f)` (CodeGenFunction::EmitCallee). A cleanup attribute's call is one too
// (CallCleanupFunction::Emit, #147's CHECK_CONTROL_CLEANUP): of the function its argument names, with that function's
// attributes from its declarations before the argument, reported at the attribute (its first token:
// [[gnu::cleanup(f)]]'s scope's), and only where the variable's own declaration was emitted — an `if (0)` arm's is,
// its cleanup's call among them, where a label's jump could enter it — and once for each variable whose scope an exit
// the walk reaches leaves: at the
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
// differ (CheckTargetFeatureEmission): nothing after an expression statement that ends in a call that doesn't return —
// one through `*` or `&` to a noreturn function's own declaration, or of one of the C library's functions clang knows
// doesn't return (exit, abort, _Exit, _exit, longjmp and kin, the builtins' attributes, which -ffreestanding,
// -fno-builtin and -fno-builtin-NAME lift; not a function pointer variable's) — loops' blocks emitted whatever their
// conditions, and code after a label whatever precedes it: a statement that holds a label from its start (an if, loop
// or switch after a point where its code stops, or the arm of an if or a switch that a constant condition would leave
// out), and an operand a constant condition leaves out where a statement expression in it holds a label, which
// check_program_references keeps (a labeled operand is code clang emits, so its references are the body's) — a label
// inside sizeof or alignof's operand too, as ContainsLabel is syntactic. A statement expression's last expression and
// labels emit after a statement whose code stops, as EnsureInsertPoint gives the value a block of its own. The
// library builtins' noreturn merges into the user's declaration whatever its result type, but longjmp's named types
// must be file-scope typedefs before the declaration in effect at the call. A switch
// on a constant emits just the case the value takes where its scan can skip the rest — no label a skipped statement
// holds, no declaration at the body's scope, no case range the list's order puts first, no kept statement's own break
// — and the whole body where it can't (docs/x86-target.md), an unlabeled statement after a dead one skipped either way.
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

typedef struct CheckTargetFeatureEmission CheckTargetFeatureEmission;

// CheckTargetFeatureEmission.subtree_bits: the subtree holds a label, a case of a switch outside it is implied by
// min_case_owner, and it holds a `do` (check_target_feature_label_root's terminator test).
#define BUSTER_CHECK_TARGET_FEATURE_LABEL 1u
#define BUSTER_CHECK_TARGET_FEATURE_DO 2u

// CheckTargetFeatureEmission.type_names: `jmp_buf` and `sigjmp_buf`, the typedefs the noreturn library builtins'
// signatures' named types resolve to (Sema::LookupNecessaryTypesForBuiltin).
#define BUSTER_CHECK_TARGET_FEATURE_TYPE_NAMES 2u
#define BUSTER_CHECK_TARGET_FEATURE_JMP_BUF 0u
#define BUSTER_CHECK_TARGET_FEATURE_SIGJMP_BUF 1u

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
    const CheckValue value = callee < graph->count ? check_stream_value(stream, callee) : (CheckValue){ 0 };
    uint32_t object = overload != BUSTER_CHECK_NONE ? stream->overloads[overload].selected.object : 0;
    if (overload == BUSTER_CHECK_NONE && callee < graph->count && graph->ops[callee] == CHECK_OP_VALUE && value.category == CHECK_FUNCTION)
    {
        object = value.object;
    }
    return object < program->object_count ? object : 0;
}

// The call designator's function for the doesn't-return question: the function its expression names, through `*` and
// `&` (CallExpr::getCalleeDecl, which strips them; EmitCallee's directness is the feature check's, not this one's).
static uint32_t check_target_feature_noreturn_object(const CheckProgram* program, uint32_t node, uint32_t* name)
{
    const CheckGraph* graph = &program->input->graph;
    uint32_t operand = graph->operand0[node];
    uint32_t steps = 0;
    while (operand < graph->count && (graph->ops[operand] == CHECK_OP_DEREFERENCE || graph->ops[operand] == CHECK_OP_ADDRESS) &&
           steps++ < graph->count)
    {
        operand = graph->operand0[operand];
    }
    const CheckValue value = operand < graph->count ? check_stream_value(program->stream, operand) : (CheckValue){ 0 };
    *name = operand < graph->count ? graph->tokens[operand] : 0;
    return operand < graph->count && graph->ops[operand] == CHECK_OP_VALUE && value.category == CHECK_FUNCTION && value.object < program->object_count ?
               value.object :
               0;
}

// Whether a call of `object` doesn't return by the function's name alone: one of the C library's functions clang's
// builtin table says doesn't ('r' in BuiltinIntrinsicInfo.attributes: exit, abort, _Exit, _exit, longjmp's and their
// kin), where the name is a library builtin of the unit as clang's builtin resolution makes it: a function with
// external linkage, under the options that keep the library builtins (check_input_library_enabled's gate: the
// language's, no features, not -fno-builtin, -ffreestanding or -fno-builtin-NAME). The callee's own noreturn
// attribute, which needs no name, is asked separately.
//
// It isn't the declaration's signature that decides but the builtin declaration clang creates to redeclare it
// (Sema::LazilyCreateBuiltin), whose noreturn the redeclaration merge propagates (SemaDecl.cpp's
// mergeTypeWithPrevious: `OldTypeInfo.getNoReturn() && !NewTypeInfo.getNoReturn()`): the declaration merges it
// whatever its type — `void exit(void *)` and `int exit(int)` both don't return — where the builtin's type can be
// built at all. That needs the typedef names its signature's letters name (LookupNecessaryTypesForBuiltin: `jmp_buf`,
// `sigjmp_buf`) at file scope before the function's declaration, or clang's allowTypeMismatch still leaves the
// declaration alone — `void longjmp(void *, int)` without `jmp_buf` returns. The declaration is the one the call's
// callee resolves to (`use`, `CheckInput.declarations`): a later redeclaration that gains noreturn from a `jmp_buf`
// declared since doesn't make an earlier call one.
static bool check_target_feature_library_noreturn(const CheckProgram* program, const PPOptions* options, const uint32_t* type_names,
                                                  uint32_t object, uint32_t use)
{
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    const uint32_t resolved = use < input->token_count && input->declarations != NULL ? input->declarations[use] : BUSTER_CHECK_NONE;
    const uint32_t declaration = resolved != BUSTER_CHECK_NONE ? resolved :
                                 object < program->object_count ? program->object_declarations[object] : BUSTER_CHECK_NONE;
    const bool function = declaration < parser->tokens.count && input->declaration_kinds[declaration] == C_BINDING_FUNCTION &&
                          input->linkage[declaration] == 2;
    const uint32_t name = function ? parser->tokens.names[declaration] : 0;
    const BuiltinIntrinsicInfo* info = name != 0 ? builtin_intrinsic_lookup((const char*)intern_bytes(program->identifiers, name),
                                                                          intern_length(program->identifiers, name)) :
                                               NULL;
    const bool library = info != NULL && memchr(info->attributes, 'f', info->attributes_length) != NULL &&
                         memchr(info->attributes, 'r', info->attributes_length) != NULL;
    bool noreturn = library && check_input_library_enabled(info, options);
    if (noreturn)
    {
        // The named types the builtin's signature needs (LookupNecessaryTypesForBuiltin): each must be declared before
        // the function's declaration, or the builtin's declaration is never made to merge noreturn from.
        for (uint32_t e = 0; noreturn && e < info->encoding_length; e += 1)
        {
            const uint32_t needed = e != 0 && info->encoding[e - 1] == 'S' && info->encoding[e] == 'J' ?
                                        BUSTER_CHECK_TARGET_FEATURE_SIGJMP_BUF :
                                    info->encoding[e] == 'J' ? BUSTER_CHECK_TARGET_FEATURE_JMP_BUF :
                                        BUSTER_CHECK_TARGET_FEATURE_TYPE_NAMES;
            noreturn = needed == BUSTER_CHECK_TARGET_FEATURE_TYPE_NAMES ||
                       (type_names != NULL && type_names[needed] < declaration);
        }
    }
    return noreturn;
}

// Whether x86 builtin call `node` doesn't return, as clang's EmitBuiltinExpr of a builtin whose attributes say so
// leaves nothing reachable after it ('r' in BuiltinIntrinsicInfo.attributes: __builtin_abort, __builtin_longjmp,
// __builtin_eh_return, __builtin_verbose_trap; __builtin_trap and __builtin_unreachable go by CHECK_OP_BUILTIN_MISC).
static bool check_target_feature_intrinsic_noreturn(const CheckProgram* program, uint32_t node)
{
    const CheckInput* input = program->input;
    const uint32_t closing = input->graph.tokens[node];
    const uint32_t id = input->intrinsic_ids != NULL && closing < input->token_count ? input->intrinsic_ids[closing] : 0;
    const CheckInputIntrinsicCall* call = id != 0 && id <= input->intrinsic_call_count ? input->intrinsic_calls + id - 1 : NULL;
    const BuiltinIntrinsicInfo* info = call != NULL && call->declaration < input->intrinsic_declaration_count ?
                                       input->intrinsic_declarations[call->declaration].info :
                                       NULL;
    return info != NULL && memchr(info->attributes, 'r', info->attributes_length) != NULL;
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
// or a call in a constant context (CheckX86Call.constant_evaluated_context), which it evaluates and never emits. An
// argument's constant counts where clang's evaluation of it has no side effect (!hasSideEffects(): not `(f(), 5)`'s)
// and no floating work under FENV_ACCESS (BUSTER_CHECK_FP_ENVIRONMENT), whose inexact operations clang doesn't fold
// (checkFloatingPointResult). A fault clang's evaluation stops at (a division by zero) leaves the argument no constant,
// and one it notes and goes past (a signed overflow, a shift's count) the constant GNU's extension gives it. Where
// clang folds such a call and the checker doesn't, the lowering refuses it by name.
static const BuiltinIntrinsicInfo* check_target_feature_emitted(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t effect = check_program_builtin_index(stream->x86, stream->x86_count, sizeof(CheckBuiltinX86Effect), node);
    const uint32_t call = check_program_builtin_index(graph->x86_calls, graph->x86_count, sizeof(CheckX86Call), node);
    const BuiltinIntrinsicInfo* info = effect != BUSTER_CHECK_NONE ? stream->x86[effect].info : NULL;
    const uint8_t kind = info != NULL ? check_action_kind(program->types, check_stream_type(stream, node)) : BUSTER_TYPE_KIND_NONE;
    bool folded = info != NULL && info->constant_evaluable_attribute && (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING);
    for (uint32_t i = 0; folded && i < graph->argument_counts[node]; i += 1)
    {
        const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
        const CheckConstantKind constant = argument < graph->count ? check_stream_constant_kind(stream, argument) : CHECK_CONSTANT_NONE;
        folded = (constant == CHECK_CONSTANT_INTEGER || constant == CHECK_CONSTANT_FLOATING) &&
                 (check_stream_flags(stream, argument) & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
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
    const bool past = predicate < graph->count && check_stream_constant_kind(stream, predicate) == CHECK_CONSTANT_INTEGER && check_stream_value(stream, predicate).integer > 7;
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
// - A label begins a block whatever code precedes it: each of the body's source labels is a root of the walk
//   (CheckProgram.label_positions), a statement expression's own where its expression's statement emits
//   (check_target_feature_emission_prepare's extra edges), and a statement that holds one emits from its start, as
//   clang's ContainsLabel leaves no statement's code out.
struct CheckTargetFeatureEmission
{
    // The dead-end analysis's bits per node, its stack of `capacity` items (each node at most twice: before and after
    // its operands), and the stack's live length: the region analysis's calls run inside the body's own, so a call
    // keeps the tail beyond `depth` and the length restores on return.
    uint8_t* bits;
    uint64_t* stack;
    uint64_t capacity;
    uint64_t depth;
    // The body's extra edges: per control, the first control that its statement's code always emits beside it, and per
    // such control, the next of the same control's, BUSTER_CHECK_NONE ending each list.
    uint32_t* first;
    uint32_t* next;
    // Per statement expression, whether its block's emission reaches its end's block through a path with a
    // predecessor: 1 where it does (its statement's code goes on), 2 where it doesn't (an orphan block, as clang's
    // EnsureInsertPoint leaves the result's, or none). check_target_feature_emission_prepare computes the body's
    // regions innermost first, so the analysis never descends into a statement expression's own walk.
    uint8_t* regions;
    // The parse tree's subtree answers, computed once per program (check_target_feature_columns): per token, its
    // subtree's first token (the extent's low end) and last token (the high end), the subtree bits below, and the
    // smallest switch token owning a case label inside the subtree, BUSTER_CHECK_NONE for none; and its first child
    // and next sibling, which check_target_feature_arms reads. The C driver's parse is tree_only and the reader's own
    // columns are its scratch's, so the check derives the three columns it keeps from the parents.
    uint32_t* extent_first;
    uint32_t* ends;
    uint32_t* first_child;
    uint32_t* next_child;
    uint8_t* subtree_bits;
    uint32_t* min_case_owner;
    // A prefix count of the unit's labels (check_target_feature_label_between's ranges are arbitrary, not subtrees).
    uint32_t* label_prefix;
    // Per expression, whether its subtree holds a label: a statement expression's whose block does
    // (check_target_feature_labeled's answer, memoized over the graph, whose operands precede their node). A `sizeof`
    // or `__alignof__` operand's labels count, as ContainsLabel is syntactic.
    uint8_t* node_labeled;
    // The typedefs the library builtins' types need (clang's LookupNecessaryTypesForBuiltin): `jmp_buf` for longjmp's
    // and `sigjmp_buf` for siglongjmp's, each its first file-scope name token's declaration, or BUSTER_CHECK_NONE
    // where the unit declares none. BUSTER_CHECK_TARGET_FEATURE_TYPE_NAMES entries.
    uint32_t* type_names;
    // The unit's options, for the library builtins' gate (check_target_feature_library_noreturn).
    const PPOptions* options;
};

#define BUSTER_CHECK_TARGET_FEATURE_DEAD 1u
#define BUSTER_CHECK_TARGET_FEATURE_STRAIGHT 2u
// The analysis computed the node's bits (emission->bits's memo: a node with none of the other bits still counts).
#define BUSTER_CHECK_TARGET_FEATURE_COMPUTED 4u

// Whether ?: `node` is a select, which branches nowhere (ScalarExprEmitter's isCheapEnoughToEvaluateUnconditionally):
// both its arms constants, of a scalar type or void.
static bool check_target_feature_select(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t flags = BUSTER_CHECK_ICE | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_GNU_CONSTANT;
    const uint32_t arms[] = { graph->operand1[node], graph->operand2[node] };
    bool constant = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(arms); i += 1)
    {
        constant = constant && arms[i] < graph->count && (check_stream_flags(stream, arms[i]) & flags) != 0 &&
                   (check_stream_flags(stream, arms[i]) & BUSTER_CHECK_SIDE_EFFECT) == 0;
    }
    const uint32_t kind = type_kind(program->types, type_strip_aligned(program->types, check_stream_type(stream, node)));
    return constant && kind != BUSTER_TYPE_KIND_STRUCT && kind != BUSTER_TYPE_KIND_UNION && kind != BUSTER_TYPE_KIND_COMPLEX;
}

// Whether code generation's code of `node` joins branches, whatever precedes it (its operands then don't decide where
// its statement ends): &&, || and ?: whose condition isn't a constant, but a ?: that is a select, va_arg (the System V
// ABI's register and stack paths), and a statement expression with statements before its value.
static bool check_target_feature_join(CheckTargetFeatureEmission* emission, CheckProgram* program, uint32_t node)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const CheckOp op = graph->ops[node];
    bool truth = false;
    const bool decided = op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL;
    const bool select = op == CHECK_OP_CONDITIONAL && check_target_feature_select(program, node);
    const uint32_t condition = decided ? graph->operand0[node] : BUSTER_CHECK_NONE;
    const bool labeled = condition < graph->count && emission->node_labeled[condition] != 0;
    const uint32_t region = op == CHECK_OP_STATEMENT_VALUE && graph->tokens[node] < input->token_count ?
                            input->expression_regions[graph->tokens[node]] : 0;
    const bool statements = region != 0 && (region > program->region_count || program->regions[region - 1].count != 1);
    return (decided && !select && (labeled || !check_program_runtime_truth(program, node, graph->operand0[node], &truth))) ||
           op == CHECK_OP_VARARGS || statements;
}

// The `k`th operand of `node` that code generation's code evaluates, in its order, or BUSTER_CHECK_NONE: an assignment's
// right side before its left, a call's callee before its arguments, the operand a constant condition selects alone, and
// none of a join's (check_target_feature_join), nor of an operand that isn't evaluated (sizeof's, a builtin's query).
static uint32_t check_target_feature_operand(CheckTargetFeatureEmission* emission, CheckProgram* program, uint32_t node, uint32_t k)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const CheckOp op = graph->ops[node];
    uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    bool truth = false;
    const bool assignment = op == CHECK_OP_ASSIGN || (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN);
    const bool decided = op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL;
    const bool labeled = decided && operands[0] < graph->count && emission->node_labeled[operands[0]] != 0;
    uint32_t result = BUSTER_CHECK_NONE;
    if (op == CHECK_OP_VALUE || op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF || op == CHECK_OP_BUILTIN_QUERY || check_target_feature_join(emission, program, node))
    {
        result = BUSTER_CHECK_NONE;
    }
    else if (op == CHECK_OP_STATEMENT_VALUE)
    {
        const uint32_t region = graph->tokens[node] < input->token_count ? input->expression_regions[graph->tokens[node]] : 0;
        result = k == 0 && region != 0 && region <= program->region_count ? program->regions[region - 1].result : BUSTER_CHECK_NONE;
    }
    else if (decided && !labeled && check_program_runtime_truth(program, node, operands[0], &truth))
    {
        // `1 && x` and `0 || x` emit x alone, `0 && x` and `1 || x` nothing; `c ? a : b` the arm c selects — a label
        // inside the condition keeps the fold out, so every operand's code goes.
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
// return — by the callee's declarations before the call, by __builtin_unreachable() or a builtin that doesn't, or by
// the library's functions clang knows as such (exit, abort, by name and external linkage) — or where an operand's
// code does and only straight-line code follows, or a statement expression's block ends in one
// (check_target_feature_region_dead). Without room on its stack, false: the statement goes on.
static bool check_target_feature_dead_end(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, uint32_t root);

// Whether the body's control `index`, visited, always leaves its code into the next one's.
static bool check_target_feature_falls(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, uint32_t index);

// Whether statement expression `region`'s emission reaches its end's block through a path with a predecessor
// (CheckTargetFeatureEmission.regions): clang's EmitCompoundStmtWithoutScope evaluates the block's last expression
// where the block's code ends, or in a fresh block that nothing reaches (EnsureInsertPoint) where it doesn't, which
// the statement's end erases — its code doesn't go on. A label's block has a predecessor where a jump or a fall-
// through that emitted reaches it — one entered where the code before it stopped has none — and a block a goto's
// emitted edge reaches has one whatever the goto's own block leaves.
static bool check_target_feature_region_dead(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, uint32_t region)
{
    CheckProgram* program = walk->program;
    const CheckProgramBody* body = walk->body;
    const CheckControl* controls = program->controls + body->first;
    const CheckProgramRegion* item = program->regions + region - 1;
    const uint32_t kept = item->count - item->cleanups;
    const uint32_t end = kept != 0 ? item->first + kept - 1 : BUSTER_CHECK_NONE;
    const uint64_t mark = walk->calls->length;
    uint8_t* states = BUSTER_ARENA_PUSH_ARRAY(walk->calls, uint8_t, body->count);
    uint32_t* stack = BUSTER_ARENA_PUSH_ARRAY(walk->calls, uint32_t, (uint64_t)body->count * 2);
    bool valid = states != NULL && stack != NULL;
    uint32_t depth = 0;
    if (valid)
    {
        memset(states, 0, (uint64_t)body->count);
        stack[depth++] = item->first;
        states[item->first] = 2;
        // A label's block without an emitted predecessor: emitted, but an orphan.
        for (uint32_t l = 0; l < body->label_count; l += 1)
        {
            const uint32_t position = program->label_positions[body->label_first + l];
            if (position >= item->first && position < item->first + item->count && states[position] == 0)
            {
                states[position] = 1;
                stack[depth++] = position;
            }
        }
    }
    while (valid && depth != 0)
    {
        const uint32_t index = stack[--depth];
        const CheckControl* entry = controls + index;
        const uint8_t state = states[index];
        if (entry->kind == CHECK_CONTROL_EVALUATE)
        {
            // The expression's code ends the block where it ends in a block with no predecessors: nothing follows it.
            // Where it is straight-line, the next statement's block is this one, whose state stands; a join's block has
            // its branches' predecessors.
            const bool ends = check_target_feature_dead_end(walk, emission, entry->node);
            const uint8_t next = ends ? 0 : (emission->bits[entry->node] & BUSTER_CHECK_TARGET_FEATURE_STRAIGHT) != 0 ? state : 2;
            if (!ends && index + 1 < item->first + item->count && states[index + 1] < next)
            {
                states[index + 1] = next;
                stack[depth++] = index + 1;
            }
        }
        else if (entry->kind == CHECK_CONTROL_JUMP && entry->target < item->first + item->count)
        {
            // The goto's edge emits whatever its own block leaves: its target has a predecessor.
            if (states[entry->target] < 2)
            {
                states[entry->target] = 2;
                stack[depth++] = entry->target;
            }
        }
        else if (entry->kind >= CHECK_CONTROL_BRANCH_FALSE)
        {
            const uint32_t targets[] = { index + 1, entry->target };
            for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(targets); t += 1)
            {
                if (targets[t] < item->first + item->count && states[targets[t]] < 2)
                {
                    states[targets[t]] = 2;
                    stack[depth++] = targets[t];
                }
            }
        }
        else if (index + 1 < item->first + item->count && states[index + 1] < state)
        {
            states[index + 1] = state;
            stack[depth++] = index + 1;
        }
    }
    const bool dead = states != NULL && end < body->count && states[end] != 2;
    arena_pop_to(walk->calls, mark);
    return valid && dead;
}

static bool check_target_feature_dead_end(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, uint32_t root)
{
    CheckProgram* program = walk->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const uint64_t base = emission->depth;
    uint64_t depth = base;
    bool room = root < graph->count && depth < emission->capacity;
    if (room)
    {
        emission->stack[depth++] = root;
    }
    while (room && depth != base)
    {
        const uint64_t item = emission->stack[--depth];
        emission->depth = depth;
        const uint32_t node = (uint32_t)item;
        uint32_t operand = 0;
        if ((emission->bits[node] & BUSTER_CHECK_TARGET_FEATURE_COMPUTED) != 0)
        {
            continue;
        }
        if ((item >> 32) == 0)
        {
            // The node's turn after its operands', which go on the stack above it.
            emission->stack[depth++] = ((uint64_t)1 << 32) | node;
            for (uint32_t k = 0; room && (operand = check_target_feature_operand(emission, program, node, k)) != BUSTER_CHECK_NONE; k += 1)
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
            const bool join = check_target_feature_join(emission, program, node);
            bool dead = false;
            bool straight = !join;
            for (uint32_t k = 0; (operand = check_target_feature_operand(emission, program, node, k)) != BUSTER_CHECK_NONE; k += 1)
            {
                const uint8_t bits = emission->bits[operand];
                dead = (bits & BUSTER_CHECK_TARGET_FEATURE_DEAD) != 0 || (dead && (bits & BUSTER_CHECK_TARGET_FEATURE_STRAIGHT) != 0);
                straight = straight && (bits & BUSTER_CHECK_TARGET_FEATURE_STRAIGHT) != 0;
            }
            const CheckOp op = graph->ops[node];
            // The doesn't-return question's callee is the function the designator names through `*` and `&`, as
            // clang's getCalleeDecl strips them — a wider reach than the feature check's direct callee.
            uint32_t name = 0;
            const uint32_t callee = op == CHECK_OP_CALL ? check_target_feature_noreturn_object(program, node, &name) : 0;
            const bool noreturn = callee != 0 && (check_target_feature_attributes(program, callee, name).noreturn ||
                                                (emission->options != NULL &&
                                                 check_target_feature_library_noreturn(program, emission->options, emission->type_names, callee, name)));
            const bool unreachable = op == CHECK_OP_BUILTIN_MISC && graph->members[node] == CBUILTIN_UNREACHABLE;
            const bool builtin = op == CHECK_OP_BUILTIN_X86 && check_target_feature_intrinsic_noreturn(program, node);
            const uint32_t region = op == CHECK_OP_STATEMENT_VALUE && graph->tokens[node] < input->token_count ?
                                    input->expression_regions[graph->tokens[node]] : 0;
            const bool region_dead = region != 0 && region <= program->region_count && emission->regions[region - 1] == 2;
            // A join's branches still go on, but a statement expression's whose block ends dead has none left.
            dead = (!join || region_dead) && (dead || noreturn || unreachable || builtin || region_dead);
            emission->bits[node] = (uint8_t)((dead ? BUSTER_CHECK_TARGET_FEATURE_DEAD : 0) |
                                             (straight ? BUSTER_CHECK_TARGET_FEATURE_STRAIGHT : 0) |
                                             BUSTER_CHECK_TARGET_FEATURE_COMPUTED);
        }
    }
    emission->depth = base;
    return room && (emission->bits[root] & BUSTER_CHECK_TARGET_FEATURE_DEAD) != 0;
}

// The tokens `low`..`high` of `token`'s subtree: the parse tree nests, so a token's descendants are the contiguous
// tokens whose ancestors pass through it — extent_first's, ending at the parser's ends, computed once
// (check_target_feature_columns).
static void check_target_feature_extent(const CheckTargetFeatureEmission* emission, const CheckProgram* program, uint32_t token,
                                        uint32_t* low, uint32_t* high)
{
    *low = token < program->parser->tokens.count ? emission->extent_first[token] : 0;
    *high = token < program->parser->tokens.count ? emission->ends[token] : 0;
}

// Whether a jump can enter `token`'s subtree other than at its start: it holds a label, or a case of a switch outside
// it (check_control_enterable, clang's ContainsLabel). The subtree's answers are computed once: a label inside it, or
// a case label whose switch is outside it, which is an owner before the subtree's first token — a case's owner is
// its ancestor, so it cannot come after the subtree's end.
static bool check_target_feature_enterable(const CheckTargetFeatureEmission* emission, const CheckProgram* program, uint32_t token)
{
    return token < program->parser->tokens.count &&
           ((emission->subtree_bits[token] & BUSTER_CHECK_TARGET_FEATURE_LABEL) != 0 ||
            emission->min_case_owner[token] < emission->extent_first[token]);
}

// Whether `token`'s declaration is at file scope: its parent chain reaches a declaration's terminator whose own parent
// is the translation unit's root. A typedef in a function body reaches that body's closing brace instead
// (check_target_feature_columns's `jmp_buf` scan).
static bool check_target_feature_file_scope(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t parent = token < count ? parser->tree.parents[token] : BUSTER_PARSE_ROOT;
    uint32_t steps = 0;
    while (parent < count && parser->tree.roles[parent] != PARSE_ROLE_TERMINATOR && steps++ < count)
    {
        parent = parser->tree.parents[parent];
    }
    return parent < count && parser->tree.parents[parent] == BUSTER_PARSE_ROOT;
}

// The answers `check_target_feature_columns` computes once for the program, where the check's repeated parent walks
// and token scans were quadratic in a deeply nested function (#891): each token's subtree's first token, the subtree
// bits (a label inside; a `do`), each subtree's smallest case owner, the labels' prefix count, and each expression's
// label answer. Regions' statement expressions nest only inward, so a region's answer's parts precede it in the
// postorder; an expression's operands precede it in the graph. Returns false without room.
static bool check_target_feature_columns(CheckProgram* program, CheckTargetFeatureEmission* emission, Arena* scratch)
{
    const Parser* parser = program->parser;
    const CheckInput* input = program->input;
    const uint32_t count = parser->tokens.count;
    bool valid = true;
    if (emission->extent_first == NULL)
    {
        const uint64_t slots = (uint64_t)count + 1;
        emission->extent_first = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
        emission->ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
        emission->first_child = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
        emission->next_child = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
        emission->subtree_bits = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, slots);
        emission->min_case_owner = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
        emission->label_prefix = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
        emission->type_names = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_CHECK_TARGET_FEATURE_TYPE_NAMES);
        emission->node_labeled = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, input->graph.count);
        // The postorder and the positions column parse_tree_links writes are this fill's alone: the kept columns
        // precede them in the scratch, so they go back to it after the fill.
        const uint64_t mark = scratch->length;
        uint32_t* postorder = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
        uint32_t* positions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
        uint32_t* last_children = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
        valid = emission->extent_first != NULL && emission->ends != NULL && emission->first_child != NULL &&
                emission->next_child != NULL && emission->subtree_bits != NULL && emission->min_case_owner != NULL &&
                emission->label_prefix != NULL && emission->type_names != NULL && emission->node_labeled != NULL &&
                postorder != NULL && positions != NULL && last_children != NULL;
        if (valid)
        {
            // The tree's columns the tree_only parse did not keep (parse_postorder and parse_tree_links, the checker
            // reader's own pair), which the subtree answers' bottom-up fill below consumes.
            const uint32_t written = parse_postorder(parser->tree.parents, count, last_children, postorder);
            parse_tree_links(parser->tree.parents, postorder, count, positions, emission->ends, emission->first_child,
                             emission->next_child);
            valid = written == count;
        }
        if (valid)
        {
            memset(emission->subtree_bits, 0, (uint64_t)count + 1);
            memset(emission->min_case_owner, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(emission->type_names, 0xff, BUSTER_CHECK_TARGET_FEATURE_TYPE_NAMES * sizeof(uint32_t));
            memset(emission->node_labeled, 0, input->graph.count);
            // The postorder computes the subtree answers bottom up: a token's children all precede it.
            for (uint32_t p = 0; p < count; p += 1)
            {
                const uint32_t token = postorder[p];
                const uint8_t role = parser->tree.roles[token];
                uint32_t first = token;
                uint8_t bits = role == PARSE_ROLE_LABEL ? BUSTER_CHECK_TARGET_FEATURE_LABEL :
                               role == PARSE_ROLE_DO ? BUSTER_CHECK_TARGET_FEATURE_DO : 0;
                uint32_t owner = role == PARSE_ROLE_CLOSE_CASE ? input->control_owners[token] : BUSTER_CHECK_NONE;
                for (uint32_t child = emission->first_child[token]; child != BUSTER_CHECK_NONE; child = emission->next_child[child])
                {
                    first = emission->extent_first[child] < first ? emission->extent_first[child] : first;
                    bits |= emission->subtree_bits[child];
                    owner = emission->min_case_owner[child] < owner ? emission->min_case_owner[child] : owner;
                }
                emission->extent_first[token] = first;
                emission->subtree_bits[token] = bits;
                emission->min_case_owner[token] = owner;
            }
            emission->extent_first[count] = count;
            uint32_t labels = 0;
            for (uint32_t token = 0; token < count; token += 1)
            {
                labels += parser->tree.roles[token] == PARSE_ROLE_LABEL;
                emission->label_prefix[token] = labels;
            }
            emission->label_prefix[count] = labels;
            // The library builtins' named typedefs (Sema::LookupNecessaryTypesForBuiltin): the first file-scope
            // `jmp_buf` or `sigjmp_buf` typedef declaration, which the noreturn gate compares with the function's
            // declaration. A block-scope one is a different type name and builds no builtin (Longjmp's
            // `void longjmp(void *, int)` then returns).
            static const char* const names[BUSTER_CHECK_TARGET_FEATURE_TYPE_NAMES] = { "jmp_buf", "sigjmp_buf" };
            for (uint32_t n = 0; n < BUSTER_CHECK_TARGET_FEATURE_TYPE_NAMES; n += 1)
            {
                const IdentId name = intern_find(program->identifiers, (const uint8_t*)names[n], (uint32_t)strlen(names[n]));
                for (uint32_t token = 0; name != 0 && emission->type_names[n] == BUSTER_CHECK_NONE && token < count; token += 1)
                {
                    const bool found = input->declaration_kinds[token] == C_BINDING_TYPEDEF && parser->tokens.names[token] == name &&
                                       check_target_feature_file_scope(parser, token);
                    emission->type_names[n] = found ? token : emission->type_names[n];
                }
            }
            // Each expression's label answer: an operand precedes its node, so a node's is its operands' answers' or,
            // for a statement expression, whether its block's subtree is enterable.
            for (uint32_t node = 0; node < input->graph.count; node += 1)
            {
                const CheckOp op = input->graph.ops[node];
                bool labeled = false;
                if (op == CHECK_OP_STATEMENT_VALUE && input->graph.tokens[node] < input->token_count)
                {
                    const uint32_t region = input->expression_regions[input->graph.tokens[node]];
                    labeled = region != 0 && region <= input->region_count &&
                              check_target_feature_enterable(emission, program, input->regions[region - 1].block);
                }
                else
                {
                    const uint32_t operands[] = { input->graph.operand0[node], input->graph.operand1[node], input->graph.operand2[node] };
                    for (uint32_t k = 0; !labeled && k < BUSTER_ARRAY_LENGTH(operands); k += 1)
                    {
                        labeled = operands[k] < node && emission->node_labeled[operands[k]] != 0;
                    }
                    for (uint32_t a = 0; !labeled && check_graph_has_arguments(op) && a < input->graph.argument_counts[node]; a += 1)
                    {
                        const uint32_t argument = input->graph.argument_nodes[input->graph.argument_offsets[node] + a];
                        labeled = argument < node && emission->node_labeled[argument] != 0;
                    }
                }
                emission->node_labeled[node] = labeled;
            }
        }
        // The fill's own columns go back; the kept ones were pushed before them.
        arena_pop_to(scratch, mark);
    }
    return valid;
}

// The `)` condition's arms: `*body` the statement it always runs, `*alternate` an if statement's else's, or
// BUSTER_CHECK_NONE. `paren`'s children give both directly: the arm is the child whose subtree holds the token after
// `paren`, and an else is the child so roled, whose own first child is its arm.
static void check_target_feature_arms(const CheckTargetFeatureEmission* emission, const CheckProgram* program, uint32_t paren,
                                      uint32_t* body, uint32_t* alternate)
{
    const Parser* parser = program->parser;
    const uint8_t* roles = parser->tree.roles;
    *body = BUSTER_CHECK_NONE;
    *alternate = BUSTER_CHECK_NONE;
    for (uint32_t child = paren < parser->tokens.count ? emission->first_child[paren] : BUSTER_CHECK_NONE; child != BUSTER_CHECK_NONE;
         child = emission->next_child[child])
    {
        if (emission->extent_first[child] <= paren + 1 && paren + 1 <= emission->ends[child])
        {
            *body = child;
        }
        else if (roles[child] == PARSE_ROLE_ELSE)
        {
            *alternate = emission->first_child[child];
        }
    }
}

// The control where clang emits the statement of head token `head` from its start: the statement's first control. An
// if or while statement starts at its condition's branch, a switch at its dispatch, a do statement at its body's start,
// a for statement at its init or condition, and any other statement at its first control (`index` bound).
static uint32_t check_target_feature_start(const CheckTargetFeatureEmission* emission, const CheckProgram* program,
                                           const CheckProgramBody* body, uint32_t head, uint32_t index)
{
    const Parser* parser = program->parser;
    const uint8_t* roles = parser->tree.roles;
    const uint32_t* parents = parser->tree.parents;
    const CheckControl* controls = program->controls + body->first;
    const uint32_t count = body->entry_count;
    uint32_t start = BUSTER_CHECK_NONE;
    if (roles[head] == PARSE_ROLE_TERMINATOR)
    {
        // A do statement's `;`: its body's start is the condition's branch's target. Any other statement's `;` (or a
        // declaration's) starts at its own first control.
        for (uint32_t i = 0; start == BUSTER_CHECK_NONE && i < count; i += 1)
        {
            const CheckControl* entry = controls + i;
            start = entry->kind == CHECK_CONTROL_BRANCH_TRUE && entry->token < parser->tokens.count && parents[entry->token] == head ?
                        entry->target :
                        BUSTER_CHECK_NONE;
        }
        if (start == BUSTER_CHECK_NONE)
        {
            uint32_t low = 0;
            uint32_t high = 0;
            check_target_feature_extent(emission, program, head, &low, &high);
            for (uint32_t i = 0; i < count; i += 1)
            {
                if (low <= controls[i].token && controls[i].token <= high)
                {
                    start = i;
                    break;
                }
            }
        }
    }
    else
    {
        for (uint32_t i = 0; start == BUSTER_CHECK_NONE && i < count; i += 1)
        {
            const CheckControl* entry = controls + i;
            if (entry->kind == CHECK_CONTROL_BRANCH_FALSE && (entry->token == head || parents[entry->token] == head))
            {
                // An if or a while's condition's branch, a for's after its init's controls, a switch's dispatch.
                uint32_t first = i;
                while (roles[head] == PARSE_ROLE_CLOSE_FOR && first != 0)
                {
                    uint32_t ancestor = controls[first - 1].token;
                    for (uint32_t steps = 0; ancestor < parser->tokens.count && ancestor != head && steps++ < parser->tokens.count;
                         ancestor = parents[ancestor])
                    {
                    }
                    if (ancestor != head)
                    {
                        break;
                    }
                    first -= 1;
                }
                start = roles[head] == PARSE_ROLE_CLOSE_FOR ? first : i;
            }
            else if (entry->kind == CHECK_CONTROL_SWITCH && entry->token == head)
            {
                start = i;
            }
        }
    }
    return start != BUSTER_CHECK_NONE ? start : index;
}

// The control where clang's emission of the statement that holds the body's control `position` — a label's — starts
// (ContainsLabel's whole statements): the outermost control statement it is in — an if, while, for, do or switch — is
// emitted from its start; a block, a label, a case or a statement expression holds it only through the statement that
// does, and a plain statement's from its first control.
static uint32_t check_target_feature_label_root(const CheckTargetFeatureEmission* emission, const CheckProgram* program,
                                                const CheckProgramBody* body, uint32_t position)
{
    const Parser* parser = program->parser;
    const uint8_t* roles = parser->tree.roles;
    const uint32_t* parents = parser->tree.parents;
    const CheckInput* input = program->input;
    const CheckControl* controls = program->controls + body->first;
    const uint32_t count = parser->tokens.count;
    uint32_t outermost = BUSTER_CHECK_NONE;
    uint32_t statement = BUSTER_CHECK_NONE;
    const uint32_t token = position < body->count ? controls[position].token : BUSTER_CHECK_NONE;
    for (uint32_t ancestor = token, steps = 0; ancestor < count && steps++ < count; ancestor = parents[ancestor])
    {
        const uint8_t role = roles[ancestor];
        if (role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR)
        {
            outermost = ancestor;
            statement = statement == BUSTER_CHECK_NONE ? ancestor : statement;
        }
        else if (role == PARSE_ROLE_TERMINATOR)
        {
            if ((emission->subtree_bits[ancestor] & BUSTER_CHECK_TARGET_FEATURE_DO) != 0)
            {
                outermost = ancestor;
            }
            statement = statement == BUSTER_CHECK_NONE ? ancestor : statement;
        }
        else if (role == PARSE_ROLE_CLOSE_BODY)
        {
            break;
        }
        else if (role == PARSE_ROLE_CLOSE_GROUP && input->expression_regions[ancestor] != 0)
        {
            // A statement expression's labels are its statement's to hold, not the expression's.
            continue;
        }
    }
    return outermost != BUSTER_CHECK_NONE ? check_target_feature_start(emission, program, body, outermost, position) :
           statement != BUSTER_CHECK_NONE ? check_target_feature_start(emission, program, body, statement, position) :
           position;
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
    bool valid = emission->first != 0 && emission->next != 0;
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
            // The statement that holds the label emits from its start, as clang's ContainsLabel emits a statement that
            // holds one (check_target_feature_label_root); and the label's own block emits wherever the code before it
            // stopped — EmitLabel's block always stands, a predecessor or not.
            check_x86_diagnostic_push(walk, check_target_feature_label_root(emission, program, body, position), true);
            check_x86_diagnostic_push(walk, position, true);
        }
        else if (position < body->count)
        {
            // A statement expression's own label begins a block where its expression's statement emits: an extra edge
            // of its region's first control.
            uint32_t region = BUSTER_CHECK_NONE;
            for (uint32_t r = 0; region == BUSTER_CHECK_NONE && r < program->region_count; r += 1)
            {
                const CheckProgramRegion* item = program->regions + r;
                region = item->body == (uint32_t)(body - program->bodies) && item->first <= position &&
                                 position < item->first + item->count ?
                             r + 1 :
                             BUSTER_CHECK_NONE;
            }
            if (region != BUSTER_CHECK_NONE)
            {
                const uint32_t first = program->regions[region - 1].first;
                emission->next[position] = emission->first[first];
                emission->first[first] = position;
            }
        }
    }
    // The body's statement expressions' block states, innermost first: a statement expression's own analysis asks its
    // nested regions' states, which an expression's can only reference where they nest inside it — never its own —
    // so the dead-end analysis reads finished answers, and this never recurses into it (check-call-graph). The input
    // numbers regions in source order as it reads their openings, so a nested region's id is larger than its
    // enclosing one's: descending ids are innermost first, without a sort (a deeply nested function's used to make
    // this quadratic).
    if (valid && program->region_count != 0)
    {
        for (uint32_t r = program->region_count; r-- > 0;)
        {
            const CheckProgramRegion* item = program->regions + r;
            if (item->body == (uint32_t)(body - program->bodies) && emission->regions[r] == 0)
            {
                emission->regions[r] = check_target_feature_region_dead(walk, emission, r + 1) ? 2 : 1;
            }
        }
    }
    return valid;
}

// A label a jump could enter in the body's tokens in [low, high) — what clang's ContainsLabel reports of a skipped
// statement, or of the whole switch (case labels aren't labels for it: the collection sees them itself).
static bool check_target_feature_label_between(const CheckTargetFeatureEmission* emission, const Parser* parser, uint32_t low, uint32_t high)
{
    const uint32_t count = parser->tokens.count;
    return high <= count && low <= high && emission->label_prefix[high] != emission->label_prefix[low];
}

// Whether `token`'s statement is the switch body's own of head `body` — its parents to `body` its statement ends, the
// case labels and, `allow`'s bits say, a nested compound's ends (BUSTER_CHECK_TARGET_FEATURE_TOP_COMPOUND, for a
// break, which still ends the kept statements there) or a `label:`'s and a statement's attributes
// (BUSTER_CHECK_TARGET_FEATURE_TOP_LABEL, for a declaration) — not inside a statement that opens a scope other than a
// compound's, an if's, a loop's or a switch's, which clang's collection can't skip a declaration for
// (mightAddDeclToScope, which descends compounds and labels) or keep a break under (containsBreak: a break under a
// label or a scope fails it).
#define BUSTER_CHECK_TARGET_FEATURE_TOP_LABEL 1u
#define BUSTER_CHECK_TARGET_FEATURE_TOP_COMPOUND 2u
static bool check_target_feature_top_level(const Parser* parser, uint32_t token, uint32_t body, uint32_t allow)
{
    const uint8_t* roles = parser->tree.roles;
    const uint32_t* parents = parser->tree.parents;
    bool statement = false;
    bool top = true;
    for (uint32_t ancestor = parents[token]; ancestor < parser->tokens.count && ancestor != body && top; ancestor = parents[ancestor])
    {
        const uint8_t role = roles[ancestor];
        if (!statement)
        {
            statement = role == PARSE_ROLE_TERMINATOR;
        }
        else
        {
            top = role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_CASE ||
                  ((allow & BUSTER_CHECK_TARGET_FEATURE_TOP_COMPOUND) != 0 && role == PARSE_ROLE_CLOSE_BLOCK) ||
                  ((allow & BUSTER_CHECK_TARGET_FEATURE_TOP_LABEL) != 0 &&
                   (role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER));
        }
    }
    return top;
}

// The switch's emission where its dispatch's value is a constant (clang's FindCaseStatementsForValue in
// EmitSwitchStmt): the statements of the case it selects, the default's where none does, or nothing at all — but
// where a statement the selection skips holds a label (one a jump could enter, ContainsLabel) or is a declaration
// the body's scope gains (mightAddDeclToScope), or a break something other than a compound or a case holds
// (containsBreak), the collection fails and the whole body emits instead, save the dead statements before its
// first case label.
static void check_target_feature_switch(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, uint32_t index)
{
    CheckProgram* program = walk->program;
    const Parser* parser = program->parser;
    const CheckControl* controls = program->controls + walk->body->first;
    const CheckControl* entry = controls + index;
    const uint32_t node = entry->node;
    uint32_t next = index + 1;
    unsigned __int128 value = 0;
    // A label inside the selector's expression keeps clang's ConstantFoldsToSimpleInteger from folding it: the
    // switch's cases all emit, as an unknown one's do.
    const bool known = check_program_case_value(program, node, check_stream_type(program->stream, node), &value) &&
                       !(node < program->input->graph.count && emission->node_labeled[node] != 0);
    while (next < walk->body->count && controls[next].kind == CHECK_CONTROL_CASE)
    {
        next += 1;
    }
    if (!known)
    {
        // No constant: every case's and the fallback's, as #487's walk pushes them.
        for (uint32_t i = index + 1; i < next; i += 1)
        {
            check_x86_diagnostic_push(walk, program->cases[controls[i].target].target, true);
        }
        check_x86_diagnostic_push(walk, next, true);
        return;
    }
    // clang's scan takes the switch's case list, the cases in the reverse of their source order — the entries
    // backwards — and stops at the first match; a case range it meets first fails the selection outright, which has
    // the whole body's code go, as a switch without a constant's (FindCaseStatementsForValue).
    bool whole = false;
    uint32_t selected = BUSTER_CHECK_NONE;
    uint32_t selected_token = BUSTER_CHECK_NONE;
    for (uint32_t i = next; !whole && selected == BUSTER_CHECK_NONE && i-- > index + 1;)
    {
        const CheckProgramCase* item = program->cases + controls[i].target;
        whole = item->range;
        if (!whole)
        {
            uint32_t width = 0;
            bool sign = false;
            check_action_integer_type(program->types, item->type, &width, &sign);
            const unsigned __int128 ordered = value ^ (sign ? (unsigned __int128)1 << (width - 1) : 0);
            if (ordered >= check_program_case_order(program, item, false) && ordered <= check_program_case_order(program, item, true))
            {
                selected = item->target;
                selected_token = item->token;
            }
        }
    }
    // The control after the case table jumps to the default label or the switch's end.
    const CheckProgramCase* fallback = NULL;
    for (uint32_t i = index + 1; i < next; i += 1)
    {
        const CheckProgramCase* item = program->cases + controls[i].target;
        fallback = item->fallback && item->owner == entry->token ? item : fallback;
    }
    if (!whole && selected == BUSTER_CHECK_NONE && fallback != NULL)
    {
        selected = controls[next].target;
        selected_token = fallback->token;
    }
    // The body's statements, from the control after the table's jump.
    const uint32_t statements = next + 1;
    uint32_t body = entry->token + 1;
    for (uint32_t steps = 0; body < parser->tokens.count && parser->tree.parents[body] != entry->token && steps++ < parser->tokens.count;
         body = parser->tree.parents[body])
    {
    }
    // The body's own extent and its statements' end — the control after the last whose token is inside. The walk up
    // can only miss the switch's head where the control's token isn't its `)`, a sentinel the extent can't answer.
    uint32_t low = 0;
    uint32_t high = 0;
    if (body < parser->tokens.count)
    {
        check_target_feature_extent(emission, program, body, &low, &high);
    }
    uint32_t end = statements;
    for (uint32_t i = statements; i < walk->body->count; i += 1)
    {
        end = controls[i].token >= low && controls[i].token <= high ? i + 1 : end;
    }
    uint32_t kept_control_end = end;
    if (!whole)
    {
        if (selected == BUSTER_CHECK_NONE)
        {
            // No case the value takes, no default: clang emits none of the body — unless a jump can enter it, a label
            // it holds, which emits the whole switch (its `!ContainsLabel(&S)`). Either way the code after the switch
            // goes on: the table's jump reaches it.
            whole = check_target_feature_label_between(emission, parser, low, high + 1);
        }
        else
        {
            // The kept statements, from the case the value selects or the default, end at the first break the body
            // itself makes — the switch's end, the control after the body's last. One a compound, an `if`, a label or
            // another statement holds fails the collection; so does a declaration a skipped statement adds at the
            // body's own scope — the kept ones may use it — or a label a skipped statement holds.
            uint32_t kept_end = high + 1;
            bool broke = false;
            for (uint32_t i = selected; !whole && i < end; i += 1)
            {
                if (controls[i].kind == CHECK_CONTROL_JUMP && controls[i].target == end &&
                    parser->tokens.kinds[controls[i].token] == PARSE_KIND_BREAK)
                {
                    if (check_target_feature_top_level(parser, controls[i].token, body, BUSTER_CHECK_TARGET_FEATURE_TOP_COMPOUND))
                    {
                        const uint32_t terminator = parser->tree.parents[controls[i].token];
                        kept_end = terminator < parser->tokens.count ? terminator + 1 : kept_end;
                        kept_control_end = i + 1;
                        broke = true;
                    }
                    else
                    {
                        whole = true;
                    }
                    break;
                }
            }
            // A declaration a skipped statement adds (mightAddDeclToScope): clang's collection recurses through
            // nested compounds and fails on any it skips, whether it is a case's own statement or one before the
            // first case — a declaration inside `if` or `for` is not the scan's.
            for (uint32_t token = low; !whole && token < selected_token && token <= high; token += 1)
            {
                whole = program->input->declaration_kinds[token] != 0 &&
                        check_target_feature_top_level(parser, token, body,
                                                       BUSTER_CHECK_TARGET_FEATURE_TOP_LABEL | BUSTER_CHECK_TARGET_FEATURE_TOP_COMPOUND);
            }
            // A declaration in the statements that fall out of the body's end without a break fails the collection
            // too (CollectStatementsForCase's AnyDecls): the switch body's own compound began before the case was
            // found, so it can't be emitted whole. The kept statements may use it, so the whole body's code goes.
            for (uint32_t token = selected_token; !whole && !broke && token <= high; token += 1)
            {
                whole = program->input->declaration_kinds[token] != 0 &&
                        check_target_feature_top_level(parser, token, body, BUSTER_CHECK_TARGET_FEATURE_TOP_LABEL);
            }
            whole = whole || check_target_feature_label_between(emission, parser, low, selected_token) ||
                    check_target_feature_label_between(emission, parser, kept_end, high + 1);
        }
    }
    if (whole)
    {
        // The collection failed: the whole body emits as code generation's dispatch does — a block at each case, the
        // default's or the block after the switch, and the statements a label can enter from their start. An unlabeled
        // statement after one whose code stopped is skipped (EmitStmt's insertion point), so only the first reachable
        // control and the case labels' targets go on the walk.
        uint32_t first_target = end;
        uint32_t first_case_token = high;
        for (uint32_t i = index + 1; i < next; i += 1)
        {
            const CheckProgramCase* item = program->cases + controls[i].target;
            first_target = item->target < first_target ? item->target : first_target;
            first_case_token = item->token < first_case_token ? item->token : first_case_token;
        }
        uint32_t from = first_target;
        for (uint32_t l = 0; l < walk->body->label_count; l += 1)
        {
            const uint32_t position = program->label_positions[walk->body->label_first + l];
            if (position < walk->body->entry_count && controls[position].token < first_case_token)
            {
                const uint32_t root = check_target_feature_label_root(emission, program, walk->body, position);
                from = root < from ? root : from;
            }
        }
        check_x86_diagnostic_push(walk, from, true);
        for (uint32_t i = index + 1; i < next; i += 1)
        {
            check_x86_diagnostic_push(walk, program->cases[controls[i].target].target, true);
        }
        check_x86_diagnostic_push(walk, controls[next].target, true);
    }
    else if (selected != BUSTER_CHECK_NONE)
    {
        // The kept statements walk from the first: each emits where it is, and EmitStmt's insertion-point rule skips
        // an unlabeled statement after one whose code stopped, as the walk's dead end does. The switch's merge emits
        // where the last kept control falls into it; a break's own jump reaches it, and a kept statement whose code
        // ends dead (a call that doesn't return) leaves the merge's block nothing.
        if (selected < kept_control_end)
        {
            check_x86_diagnostic_push(walk, selected, true);
        }
        if (check_target_feature_falls(walk, emission, kept_control_end - 1))
        {
            check_x86_diagnostic_push(walk, end, true);
        }
    }
    else
    {
        // The value takes no case and the body has no default: nothing of it emits, but the code after the switch does.
        check_x86_diagnostic_push(walk, controls[next].target, true);
    }
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
        if (!check_target_feature_dead_end(walk, emission, entry->node))
        {
            check_x86_diagnostic_push(walk, index + 1, true);
        }
    }
    else if (entry->kind == CHECK_CONTROL_BRANCH_TRUE || loop != 0)
    {
        // A do statement's condition, which its end follows whatever its value — clang's `br i1` reaches the exit even
        // under `while (1)`, whose block nothing else needs — or a while or for statement's, which its body follows
        // whatever its value; a constant one has no branch where it can't go: back from `do ... while (0)`, out of
        // `while (1)`. A label inside the condition keeps clang's ConstantFoldsToSimpleInteger from folding it.
        bool truth = false;
        const bool known = check_program_runtime_truth(program, entry->node, entry->node, &truth) &&
                           !(entry->node < program->input->graph.count && emission->node_labeled[entry->node] != 0);
        const bool branch = entry->kind == CHECK_CONTROL_BRANCH_TRUE ? !known || truth : loop == 2 || !known || !truth;
        check_x86_diagnostic_push(walk, entry->node, false);
        if (branch)
        {
            check_x86_diagnostic_push(walk, entry->target, true);
        }
        check_x86_diagnostic_push(walk, index + 1, true);
    }
    else if (entry->kind == CHECK_CONTROL_BRANCH_FALSE)
    {
        // An if statement's condition: the arm a constant one leaves out still emits where a jump can enter it — it
        // holds a label, or a case of a switch outside it (ConstantFoldsToSimpleInteger and ContainsLabel, which also
        // keeps the condition itself from folding where it holds a label).
        bool truth = false;
        const bool known = check_program_runtime_truth(program, entry->node, entry->node, &truth) &&
                           !(entry->node < program->input->graph.count && emission->node_labeled[entry->node] != 0);
        check_x86_diagnostic_push(walk, entry->node, false);
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t alternate = BUSTER_CHECK_NONE;
        check_target_feature_arms(emission, program, entry->token, &body, &alternate);
        if (!known)
        {
            check_x86_diagnostic_push(walk, entry->target, true);
            check_x86_diagnostic_push(walk, index + 1, true);
        }
        else
        {
            const uint32_t skipped = truth ? alternate : body;
            check_x86_diagnostic_push(walk, truth ? index + 1 : entry->target, true);
            if (skipped != BUSTER_CHECK_NONE && check_target_feature_enterable(emission, program, skipped))
            {
                check_x86_diagnostic_push(walk, truth ? entry->target : index + 1, true);
            }
        }
    }
    else if (entry->kind == CHECK_CONTROL_SWITCH)
    {
        check_x86_diagnostic_push(walk, entry->node, false);
        check_target_feature_switch(walk, emission, index);
    }
    else
    {
        check_x86_diagnostic_control(walk, index);
    }
}

// Whether `node`'s code holds a label — a statement expression's whose block does — which code generation emits even
// in the operand a constant condition leaves out: clang's ContainsLabel is the whole subtree's, and a label is a
// statement's, so an operand can hold one only through a statement expression. The answer is memoized per node
// (check_target_feature_columns).
static bool check_target_feature_labeled(CheckTargetFeatureEmission* emission, CheckProgram* program, uint32_t node)
{
    return node < program->input->graph.count && emission->node_labeled[node] != 0;
}

// The check's walk's step at expression `node`: #487's walk's, the operand a constant condition leaves out where it
// holds a label, which code generation still emits (check_target_feature_labeled), and a statement expression's final
// statement, which clang emits wherever the expression's statement emits — EnsureInsertPoint makes its block even
// where the block's own code stopped (EmitCompoundStmtWithoutScope's value).
static void check_target_feature_expression(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, uint32_t node)
{
    CheckProgram* program = walk->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL)
    {
        uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        // A label inside the condition keeps clang's ConstantFoldsToSimpleInteger from folding it: every operand's
        // code emits, whichever the constant would have left out.
        const bool labeled_condition = check_target_feature_labeled(emission, program, operands[0]);
        if (labeled_condition)
        {
            for (uint32_t k = 1; k < BUSTER_ARRAY_LENGTH(operands); k += 1)
            {
                if (operands[k] < graph->count)
                {
                    check_x86_diagnostic_push(walk, operands[k], false);
                }
            }
        }
        bool truth = false;
        if (!labeled_condition && check_program_runtime_truth(program, node, operands[0], &truth))
        {
            const uint32_t skipped = op == CHECK_OP_CONDITIONAL ? operands[truth ? 2 : 1] :
                                    truth == (op == CHECK_OP_LOGICAL_OR) ? operands[1] : BUSTER_CHECK_NONE;
            if (skipped < graph->count && check_target_feature_labeled(emission, program, skipped))
            {
                check_x86_diagnostic_push(walk, skipped, false);
            }
        }
    }
    else if (op == CHECK_OP_STATEMENT_VALUE && graph->tokens[node] < program->input->token_count)
    {
        const uint32_t region = program->input->expression_regions[graph->tokens[node]];
        if (region != 0 && region <= program->region_count && program->input->regions[region - 1].final_statement != BUSTER_CHECK_NONE)
        {
            // EmitCompoundStmtWithoutScope's value: EnsureInsertPoint gives the last expression a block of its own
            // where the block's code stopped, so it and the block's cleanups run (and their calls are checked) however
            // dead the statements before them are. The region's END control pushes the result and then its cleanups.
            const CheckProgramRegion* item = program->regions + region - 1;
            const uint32_t end = item->count > item->cleanups ? item->first + item->count - item->cleanups - 1 : BUSTER_CHECK_NONE;
            if (end < walk->body->count && program->controls[walk->body->first + end].kind == CHECK_CONTROL_END)
            {
                check_x86_diagnostic_push(walk, end, true);
            }
        }
    }
    check_x86_diagnostic_expression(walk, node);
}

// Whether the body's control `index`, visited, always leaves its code into the next one's: an expression statement's
// where its code goes on (not a dead end, check_target_feature_dead_end), and any control's but a jump's, a return's,
// a statement expression's END's, or a switch's (whose dispatch's edges are its cases', not a fall).
static bool check_target_feature_falls(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, uint32_t index)
{
    CheckProgram* program = walk->program;
    const CheckControl* entry = program->controls + walk->body->first + index;
    return entry->kind != CHECK_CONTROL_JUMP && entry->kind != CHECK_CONTROL_RETURN && entry->kind != CHECK_CONTROL_END &&
           entry->kind != CHECK_CONTROL_JUMP_INDIRECT && entry->kind != CHECK_CONTROL_SWITCH &&
           (entry->kind != CHECK_CONTROL_EVALUATE || !check_target_feature_dead_end(walk, emission, entry->node));
}

// Whether cleanup `record`'s declaration was emitted — then its call runs at each exit of its scope the walk reached;
// clang's EmitVarDecl registers it where the declaration's statement emits, and never where that statement's code is
// left out (a dead path, or a jump that enters its scope past it). A declaration with no controls of its own emits
// where the code of the statement before it goes on into the one after it: a visited control inside its tokens, or a
// visited pair of controls the walk's code joins across its first token.
static bool check_target_feature_cleanup_emitted(CheckX86DiagnosticWalk* walk, CheckTargetFeatureEmission* emission, uint32_t record)
{
    CheckProgram* program = walk->program;
    const CheckInput* input = program->input;
    const CheckProgramBody* body = walk->body;
    const CheckControl* controls = program->controls + body->first;
    const uint64_t first = (uint64_t)input->graph.count + body->first;
    uint32_t low = 0;
    uint32_t high = 0;
    check_target_feature_extent(emission, program, input->cleanups[record].declaration, &low, &high);
    bool emitted = controls[0].token < input->token_count && low < controls[0].token && walk->seen[first] != 0;
    for (uint32_t i = 0; !emitted && i < body->count; i += 1)
    {
        emitted = walk->seen[first + i] != 0 && low <= controls[i].token && controls[i].token <= high;
    }
    for (uint32_t i = 1; !emitted && i < body->count; i += 1)
    {
        emitted = walk->seen[first + i] != 0 && walk->seen[first + i - 1] != 0 && controls[i - 1].token <= low &&
                  low < controls[i].token && check_target_feature_falls(walk, emission, i - 1);
    }
    return emitted;
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
static __attribute__((unused)) bool check_program_target_features(CheckProgram* program, const PPOptions* pp, const PredefinesOptions* options, bool scalar,
                                                                  Arena* output, Arena* scratch, CheckTargetFeatureError** errors, uint32_t* count)
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
                    emission.regions = program->region_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, program->region_count) : 0;
                    emission.options = pp;
                    valid = walk.seen != 0 && walk.stack != 0 && (input->cleanup_count == 0 || walk.cleanup_tags != 0) && emission.bits != 0 &&
                            emission.stack != 0 && (program->region_count == 0 || emission.regions != 0);
                    if (valid)
                    {
                        memset(walk.seen, 0, items);
                        memset(emission.bits, 0, input->graph.count);
                    }
                    // The subtree and graph answers every body's walk shares, computed once: the per-body scratch the
                    // calls' own mark resets at each body's end does not reach them.
                    valid = valid && check_target_feature_columns(program, &emission, scratch);
                    if (valid && walk.cleanup_tags != 0)
                    {
                        memset(walk.cleanup_tags, 0, (uint64_t)input->cleanup_count * sizeof(uint32_t));
                    }
                    if (valid && emission.regions != 0)
                    {
                        memset(emission.regions, 0, program->region_count);
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
                            check_target_feature_expression(&walk, &emission, (uint32_t)item);
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
                            // The call runs only where the variable's declaration emitted (clang's registered
                            // cleanups, check_target_feature_cleanup_emitted): a reached exit alone doesn't make one.
                            if (check_target_feature_cleanup_emitted(&walk, &emission, record))
                            {
                                uint64_t* item = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 1);
                                valid = item != 0;
                                if (valid)
                                {
                                    const uint32_t statement = i < body->entry_count ? i + 1 : walk.cleanup_tags[record];
                                    *item = ((uint64_t)statement << 32) | i;
                                    item_count += 1;
                                }
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
