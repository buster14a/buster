// The call graph check: the coding style's rule against recursion over input-dependent structures, checked on the
// compiler's own call graph, for the check-call-graph command:
//
//   build/compiler check-call-graph FILE.ll...
//
// Each file is the LLVM IR text of one of the translation units build.sh compiles, as clang -S -emit-llvm -O0 -Xclang
// -disable-llvm-passes writes it (`./build.sh check-call-graph` makes them and runs the command): without any pass, not
// even the always-inliner, so a call in the IR is a call in the source. The check reads each `define` and the direct
// `call`, `invoke` and `callbr` instructions in its body, resolves each callee as the linker would (call_graph_resolve:
// a symbol of the same translation unit first, then an external one, whose strong definitions win over its weak ones;
// an alias, through any chain of them, to the function it names), builds the call graph of the defined functions, and
// finds its strongly connected components with Tarjan's algorithm, run with explicit stacks. Calls of functions defined nowhere (libc, LLVM's
// intrinsics) and calls through pointers are left out: the style allows function pointers only at the thread-entry
// boundary. A call of an ifunc, whose callee is chosen at run time, or of an alias that leads to no definition is a
// problem in the IR, which fails the check.
//
// Every component of more than one function, and every function that calls itself, is a cycle. The report names each
// with one call path that closes it, the shortest through the component's first function, and the command exits with
// status 1 if any cycle isn't in call_graph_allowed. Each entry there names the functions of one component and must say
// what bounds the recursion's depth (the style's "trivial statically bounded recursion"); an entry that matches no
// cycle fails the check too, so that the list shrinks when the recursion goes.
//
// main.c includes this file after lane.c (for its arenas) and call_graph_test.c after it. Layout, in this order: the
// allowlist (call_graph_allowed), the interface (CallGraphInput, CallGraphResult, call_graph_check), reading the IR
// (call_graph_symbol, call_graph_binding, call_graph_define, call_graph_alias, call_graph_call), the symbol table
// (call_graph_find and call_graph_resolve), the report's text (call_graph_append and the others), call_graph_check, and run_check_call_graph,
// the command.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

// OS-dependent includes
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef struct CallGraphAllowed CallGraphAllowed;
struct CallGraphAllowed
{
    // The names of the component's functions, separated by single spaces, in any order. A component is allowed when its
    // functions' names are the entry's names, each as many times (static functions of the same name in two translation
    // units are named twice), so every translation unit's copy of the same static functions matches one entry, and a
    // cycle that grows is a new cycle.
    String8 functions;
};

// The recursion the style permits. Each entry is preceded by a comment saying what bounds its depth.
static const CallGraphAllowed call_graph_allowed[] = {};

typedef struct CallGraphInput CallGraphInput;
struct CallGraphInput
{
    // The name the report gives the translation unit, and its IR text.
    String8 name;
    String8 text;
};

typedef struct CallGraphResult CallGraphResult;
struct CallGraphResult
{
    // The report: a line for each problem in the IR, each cycle and each unused allowlist entry, then a summary line.
    String8 report;
    uint64_t function_count;
    // Calls of defined functions, one for each call instruction.
    uint64_t call_count;
    uint64_t cycle_count;
    uint64_t allowed_count;
    uint64_t unused_allowed_count;
    uint64_t problem_count;
    // The arena was too small for the graph or the report, which is then incomplete.
    bool full;
    // No problem, no cycle outside the allowlist, every entry used, and the arena large enough.
    bool success;
};

// Checks the call graph of `inputs`' translation units against `allowed`. The report is pushed into `arena`, which the
// caller pops; the graph is built in a scratch arena.
static CallGraphResult call_graph_check(Arena* arena, const CallGraphInput* inputs, uint32_t input_count, const CallGraphAllowed* allowed, uint64_t allowed_count);

// ---------------------------------------------------------------------------------------------------------------------
// Reading the IR

// Where a function's name is looked up first: the translation unit's own, then the external ones.
#define BUSTER_CALL_GRAPH_EXTERNAL UINT32_MAX
#define BUSTER_CALL_GRAPH_NONE UINT32_MAX

static bool call_graph_starts_with(String8 line, uint64_t at, String8 prefix)
{
    return at <= line.length && prefix.length <= line.length - at && memcmp(line.pointer + at, prefix.pointer, prefix.length) == 0;
}

static bool call_graph_equal(String8 a, String8 b)
{
    return a.length == b.length && memcmp(a.pointer, b.pointer, a.length) == 0;
}

typedef struct CallGraphSymbol CallGraphSymbol;
struct CallGraphSymbol
{
    // The name without its sigil, and without the quotes of a quoted one (whose escapes are kept as they are).
    String8 name;
    // Just past the symbol.
    uint64_t end;
    bool valid;
};

// The symbol whose sigil (`@` or `%`) is at line[at].
static CallGraphSymbol call_graph_symbol(String8 line, uint64_t at)
{
    CallGraphSymbol result = { 0 };
    uint64_t begin = at + 1;
    uint64_t end = begin;
    if (begin < line.length && line.pointer[begin] == '"')
    {
        begin += 1;
        end = begin;
        while (end < line.length && line.pointer[end] != '"')
        {
            end += 1;
        }
        result.valid = end < line.length;
        result.end = end + 1;
    }
    else
    {
        while (end < line.length && (((line.pointer[end] | 0x20) >= 'a' && (line.pointer[end] | 0x20) <= 'z') || (line.pointer[end] >= '0' && line.pointer[end] <= '9') ||
                                     line.pointer[end] == '_' || line.pointer[end] == '.' || line.pointer[end] == '$' || line.pointer[end] == '-'))
        {
            end += 1;
        }
        result.valid = end > begin;
        result.end = end;
    }
    result.name = (String8){ .pointer = line.pointer + begin, .length = end - begin };
    return result;
}

typedef enum CallGraphBinding
{
    // The name is external, and this definition is the one the linker takes.
    CALL_GRAPH_BINDING_STRONG,
    // The name is external, and a strong definition of it, anywhere, wins over this one.
    CALL_GRAPH_BINDING_WEAK,
    // internal or private: the name belongs to its translation unit.
    CALL_GRAPH_BINDING_LOCAL,
} CallGraphBinding;

// The binding of the definition whose linkage, if it has one, starts at line[at]. weak, linkonce and their _odr forms
// and available_externally (a copy of a definition elsewhere) give way to a strong definition.
static CallGraphBinding call_graph_binding(String8 line, uint64_t at)
{
    CallGraphBinding result = CALL_GRAPH_BINDING_STRONG;
    if (call_graph_starts_with(line, at, BUSTER_S8("internal ")) || call_graph_starts_with(line, at, BUSTER_S8("private ")))
    {
        result = CALL_GRAPH_BINDING_LOCAL;
    }
    else if (call_graph_starts_with(line, at, BUSTER_S8("weak ")) || call_graph_starts_with(line, at, BUSTER_S8("weak_odr ")) ||
             call_graph_starts_with(line, at, BUSTER_S8("linkonce ")) || call_graph_starts_with(line, at, BUSTER_S8("linkonce_odr ")) ||
             call_graph_starts_with(line, at, BUSTER_S8("available_externally ")))
    {
        result = CALL_GRAPH_BINDING_WEAK;
    }
    return result;
}

typedef struct CallGraphDefine CallGraphDefine;
struct CallGraphDefine
{
    String8 name;
    bool is_define;
    CallGraphBinding binding;
    // A define line without a name.
    bool malformed;
};

// A function definition's first line: `define [linkage] ... @name(...) ... {`. No return type or attribute before the
// name holds an `@`, so the first one starts the name.
static CallGraphDefine call_graph_define(String8 line)
{
    CallGraphDefine result = { 0 };
    if (call_graph_starts_with(line, 0, BUSTER_S8("define ")))
    {
        result.is_define = true;
        const uint64_t linkage = BUSTER_COMPILE_TIME_STRING_LENGTH("define ");
        result.binding = call_graph_binding(line, linkage);
        uint64_t at = linkage;
        while (at < line.length && line.pointer[at] != '@')
        {
            at += 1;
        }
        CallGraphSymbol symbol = { 0 };
        if (at < line.length)
        {
            symbol = call_graph_symbol(line, at);
        }
        result.malformed = !symbol.valid || symbol.end >= line.length || line.pointer[symbol.end] != '(';
        result.name = symbol.name;
    }
    return result;
}

typedef struct CallGraphAlias CallGraphAlias;
struct CallGraphAlias
{
    String8 name;
    // The aliasee's name, or an ifunc's resolver's.
    String8 target;
    bool is_alias;
    bool ifunc;
    CallGraphBinding binding;
    // An alias or ifunc without a target.
    bool malformed;
};

// An alias or an ifunc: `@name = [linkage] [preemption] [visibility] ... (alias | ifunc) <type>, ptr @target`. The
// words before the keyword are all lowercase identifiers; a global variable's line meets `global` or `constant` first,
// before any initializer, whose strings could hold the keywords.
static CallGraphAlias call_graph_alias(String8 line)
{
    CallGraphAlias result = { 0 };
    if (line.length != 0 && line.pointer[0] == '@')
    {
        const CallGraphSymbol symbol = call_graph_symbol(line, 0);
        if (symbol.valid && call_graph_starts_with(line, symbol.end, BUSTER_S8(" = ")))
        {
            uint64_t at = symbol.end + BUSTER_COMPILE_TIME_STRING_LENGTH(" = ");
            result.binding = call_graph_binding(line, at);
            bool searching = true;
            while (searching)
            {
                uint64_t end = at;
                while (end < line.length && ((line.pointer[end] >= 'a' && line.pointer[end] <= 'z') || (line.pointer[end] >= '0' && line.pointer[end] <= '9') || line.pointer[end] == '_'))
                {
                    end += 1;
                }
                const String8 word = { .pointer = line.pointer + at, .length = end - at };
                result.is_alias = call_graph_equal(word, BUSTER_S8("alias")) || call_graph_equal(word, BUSTER_S8("ifunc"));
                result.ifunc = call_graph_equal(word, BUSTER_S8("ifunc"));
                searching = !result.is_alias && end > at && end < line.length && line.pointer[end] == ' ' && !call_graph_equal(word, BUSTER_S8("global")) &&
                            !call_graph_equal(word, BUSTER_S8("constant"));
                at = end + 1;
            }
            if (result.is_alias)
            {
                while (at < line.length && line.pointer[at] != '@')
                {
                    at += 1;
                }
                CallGraphSymbol target = { 0 };
                if (at < line.length)
                {
                    target = call_graph_symbol(line, at);
                }
                result.name = symbol.name;
                result.target = target.name;
                result.malformed = !target.valid;
            }
        }
    }
    return result;
}

typedef struct CallGraphCall CallGraphCall;
struct CallGraphCall
{
    // The callee's name, for a direct call; empty for a call through a pointer or of inline assembly.
    String8 callee;
    bool is_call;
};

// An instruction `[%result = ] [tail | musttail | notail] (call | invoke | callbr) ...`. The callee is the first symbol
// followed at once by its argument list: the return type and attributes come before it, and none of them is (a
// `%struct.S` return type is followed by a space, and a variadic callee's type `i32 (ptr, ...)` has no sigil), while the
// arguments that name functions come after it. A quote before any such symbol starts inline assembly's template.
static CallGraphCall call_graph_call(String8 line)
{
    CallGraphCall result = { 0 };
    uint64_t at = 0;
    while (at < line.length && line.pointer[at] == ' ')
    {
        at += 1;
    }
    if (at < line.length && line.pointer[at] == '%')
    {
        CallGraphSymbol value = call_graph_symbol(line, at);
        at = value.valid && call_graph_starts_with(line, value.end, BUSTER_S8(" = ")) ? value.end + BUSTER_COMPILE_TIME_STRING_LENGTH(" = ") : line.length;
    }
    if (call_graph_starts_with(line, at, BUSTER_S8("tail ")))
    {
        at += BUSTER_COMPILE_TIME_STRING_LENGTH("tail ");
    }
    else if (call_graph_starts_with(line, at, BUSTER_S8("musttail ")))
    {
        at += BUSTER_COMPILE_TIME_STRING_LENGTH("musttail ");
    }
    else if (call_graph_starts_with(line, at, BUSTER_S8("notail ")))
    {
        at += BUSTER_COMPILE_TIME_STRING_LENGTH("notail ");
    }
    result.is_call = call_graph_starts_with(line, at, BUSTER_S8("call ")) || call_graph_starts_with(line, at, BUSTER_S8("invoke ")) || call_graph_starts_with(line, at, BUSTER_S8("callbr "));
    bool searching = result.is_call;
    while (searching && at < line.length)
    {
        const char c = line.pointer[at];
        if (c == '"')
        {
            searching = false;
        }
        else if (c == '@' || c == '%')
        {
            CallGraphSymbol symbol = call_graph_symbol(line, at);
            if (symbol.valid && symbol.end < line.length && line.pointer[symbol.end] == '(')
            {
                searching = false;
                if (c == '@')
                {
                    result.callee = symbol.name;
                }
            }
            at = symbol.end > at ? symbol.end : at + 1;
        }
        else
        {
            at += 1;
        }
    }
    return result;
}

// The line that starts at `at` in `text`, without its newline.
static String8 call_graph_line(String8 text, uint64_t at)
{
    uint64_t end = at;
    while (end < text.length && text.pointer[end] != '\n')
    {
        end += 1;
    }
    return (String8){ .pointer = text.pointer + at, .length = end - at };
}

// ---------------------------------------------------------------------------------------------------------------------
// The symbol table: open addressing over a power-of-two number of slots, each a symbol's index plus one, or zero. A
// symbol is a name in a scope (a translation unit's index, or BUSTER_CALL_GRAPH_EXTERNAL), and has a list of
// definitions: functions, aliases and ifuncs, as many as the translation units define.

typedef struct CallGraphTable CallGraphTable;
struct CallGraphTable
{
    uint32_t* slots;
    uint64_t mask;
    // Each symbol's name and scope.
    const String8* names;
    const uint32_t* scopes;
};

static uint64_t call_graph_hash(uint32_t scope, String8 name)
{
    // FNV-1a over the name, then the scope.
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0; i < name.length; i += 1)
    {
        hash = (hash ^ (uint8_t)name.pointer[i]) * 0x100000001B3ull;
    }
    hash = (hash ^ scope) * 0x100000001B3ull;
    return hash ^ (hash >> 29);
}

// The slot that holds (scope, name), or the empty slot where it would go.
static uint64_t call_graph_find(const CallGraphTable* table, uint32_t scope, String8 name)
{
    uint64_t slot = call_graph_hash(scope, name) & table->mask;
    bool searching = true;
    while (searching)
    {
        const uint32_t entry = table->slots[slot];
        if (entry == 0 || (table->scopes[entry - 1] == scope && call_graph_equal(table->names[entry - 1], name)))
        {
            searching = false;
        }
        else
        {
            slot = (slot + 1) & table->mask;
        }
    }
    return slot;
}

typedef enum CallGraphKind
{
    CALL_GRAPH_KIND_FUNCTION,
    CALL_GRAPH_KIND_ALIAS,
    CALL_GRAPH_KIND_IFUNC,
} CallGraphKind;

typedef struct CallGraphSymbols CallGraphSymbols;
struct CallGraphSymbols
{
    CallGraphTable table;
    // Each symbol's first definition, and how many of its definitions are strong.
    uint32_t* first;
    uint32_t* strong_count;
    // Each definition's kind, its function's or alias's index, the next definition of its symbol, and whether it is
    // strong (a local definition is).
    uint8_t* kinds;
    uint32_t* indices;
    uint32_t* next;
    bool* strong;
    // Each alias's translation unit (where its target is looked up) and its target's name.
    const uint32_t* alias_units;
    const String8* alias_targets;
    // The resolution that last followed each alias, and the resolution's worklist of names to look up.
    uint64_t* alias_stamps;
    uint32_t* work_units;
    String8* work_names;
};

typedef struct CallGraphCallees CallGraphCallees;
struct CallGraphCallees
{
    // How many functions the call can reach.
    uint32_t count;
    // It reaches an ifunc, or only aliases that lead to no definition (to a declaration, or round a loop of aliases).
    bool ifunc;
    bool lost;
};

// The functions a call of `name` from translation unit `unit` can reach, written to `callees` unless it is 0. The name
// is the unit's own symbol if it has one, else the external symbol, whose strong definitions are the ones the linker
// takes; without one, it takes one of the weak definitions, by link order, so all of them count. An alias's target is
// looked up the same way from its unit, and each alias is followed once per resolution, which `stamp` tells apart.
static CallGraphCallees call_graph_resolve(CallGraphSymbols* symbols, uint32_t unit, String8 name, uint64_t stamp, uint32_t* callees)
{
    CallGraphCallees result = { 0 };
    bool followed = false;
    uint32_t work_count = 1;
    symbols->work_units[0] = unit;
    symbols->work_names[0] = name;
    while (work_count != 0)
    {
        work_count -= 1;
        const uint32_t work_unit = symbols->work_units[work_count];
        const String8 work_name = symbols->work_names[work_count];
        uint32_t symbol = symbols->table.slots[call_graph_find(&symbols->table, work_unit, work_name)];
        if (symbol == 0)
        {
            symbol = symbols->table.slots[call_graph_find(&symbols->table, BUSTER_CALL_GRAPH_EXTERNAL, work_name)];
        }
        const bool strong_only = symbol != 0 && symbols->strong_count[symbol - 1] != 0;
        for (uint32_t definition = symbol != 0 ? symbols->first[symbol - 1] : BUSTER_CALL_GRAPH_NONE; definition != BUSTER_CALL_GRAPH_NONE; definition = symbols->next[definition])
        {
            const uint32_t index = symbols->indices[definition];
            const bool taken = !strong_only || symbols->strong[definition];
            if (taken && symbols->kinds[definition] == CALL_GRAPH_KIND_FUNCTION)
            {
                if (callees != 0)
                {
                    callees[result.count] = index;
                }
                result.count += 1;
            }
            else if (taken && symbols->kinds[definition] == CALL_GRAPH_KIND_IFUNC)
            {
                result.ifunc = true;
            }
            else if (taken && symbols->alias_stamps[index] != stamp)
            {
                symbols->alias_stamps[index] = stamp;
                symbols->work_units[work_count] = symbols->alias_units[index];
                symbols->work_names[work_count] = symbols->alias_targets[index];
                work_count += 1;
                followed = true;
            }
        }
    }
    result.lost = followed && result.count == 0 && !result.ifunc;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The report's text, pushed byte by byte onto the end of the arena: nothing else is pushed while it is written, so it
// stays contiguous.

typedef struct CallGraphReport CallGraphReport;
struct CallGraphReport
{
    Arena* arena;
    char* begin;
    uint64_t length;
    bool full;
};

static void call_graph_append(CallGraphReport* report, String8 text)
{
    char* bytes = arena_push(report->arena, text.length, 1);
    if (bytes == 0)
    {
        report->full = true;
    }
    else
    {
        assert(bytes == report->begin + report->length);
        memcpy(bytes, text.pointer, text.length);
        report->length += text.length;
    }
}

static void call_graph_append_number(CallGraphReport* report, uint64_t number)
{
    char digits[24];
    const int length = snprintf(digits, sizeof(digits), "%llu", (unsigned long long)number);
    call_graph_append(report, (String8){ .pointer = digits, .length = (uint64_t)length });
}

// `count` and its noun, `singular` or `plural`.
static void call_graph_append_count(CallGraphReport* report, uint64_t count, String8 singular, String8 plural)
{
    call_graph_append_number(report, count);
    call_graph_append(report, BUSTER_S8(" "));
    call_graph_append(report, count == 1 ? singular : plural);
}

// A function as the report names it: its translation unit, a colon and its name.
static void call_graph_append_function(CallGraphReport* report, const CallGraphInput* inputs, const uint32_t* units, const String8* names, uint32_t function)
{
    call_graph_append(report, inputs[units[function]].name);
    call_graph_append(report, BUSTER_S8(":"));
    call_graph_append(report, names[function]);
}

// Where a problem in the IR is: a line of a translation unit.
static void call_graph_append_location(CallGraphReport* report, String8 unit, uint64_t line)
{
    call_graph_append(report, unit);
    call_graph_append(report, BUSTER_S8(":"));
    call_graph_append_number(report, line);
    call_graph_append(report, BUSTER_S8(": "));
}

static void call_graph_append_problem(CallGraphReport* report, String8 unit, uint64_t line, String8 problem)
{
    call_graph_append_location(report, unit, line);
    call_graph_append(report, problem);
    call_graph_append(report, BUSTER_S8("\n"));
}

// The number of space-separated names in an allowlist entry, and how many times `name` is one of them.
static uint64_t call_graph_allowed_count(String8 functions)
{
    uint64_t result = 0;
    for (uint64_t i = 0; i < functions.length; i += 1)
    {
        result += functions.pointer[i] != ' ' && (i == 0 || functions.pointer[i - 1] == ' ');
    }
    return result;
}

static uint64_t call_graph_allowed_occurrences(String8 functions, String8 name)
{
    uint64_t result = 0;
    uint64_t begin = 0;
    while (begin < functions.length)
    {
        uint64_t end = begin;
        while (end < functions.length && functions.pointer[end] != ' ')
        {
            end += 1;
        }
        result += call_graph_equal((String8){ .pointer = functions.pointer + begin, .length = end - begin }, name);
        begin = end + 1;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The check

// The first pass counts definitions, aliases and call instructions, to size the arrays; the second reads them. Then the
// calls are resolved and laid out by caller (callers_begin, targets), Tarjan's algorithm numbers the components, the
// functions are grouped by component, and the report goes through the functions in the order they were defined, naming
// each cyclic component at its first function.
static CallGraphResult call_graph_check(Arena* arena, const CallGraphInput* inputs, uint32_t input_count, const CallGraphAllowed* allowed, uint64_t allowed_count)
{
    CallGraphResult result = { 0 };

    uint64_t define_count = 0;
    uint64_t alias_line_count = 0;
    uint64_t call_line_count = 0;
    for (uint32_t unit = 0; unit < input_count; unit += 1)
    {
        const String8 text = inputs[unit].text;
        uint64_t at = 0;
        while (at < text.length)
        {
            const String8 line = call_graph_line(text, at);
            define_count += call_graph_define(line).is_define;
            alias_line_count += call_graph_alias(line).is_alias;
            call_line_count += call_graph_call(line).callee.length != 0;
            at += line.length + 1;
        }
    }

    // The counts index 32-bit arrays.
    const bool fits = define_count + alias_line_count < UINT32_MAX && call_line_count < UINT32_MAX;
    const uint64_t function_capacity = fits ? define_count : 0;
    const uint64_t alias_capacity = fits ? alias_line_count : 0;
    const uint64_t definition_capacity = function_capacity + alias_capacity;
    const uint64_t call_capacity = fits ? call_line_count : 0;
    // At most half the slots are full.
    uint64_t slot_count = 16;
    while (slot_count < 2 * definition_capacity)
    {
        slot_count *= 2;
    }
    Scratch scratch = scratch_begin(arena);
    Arena* work = scratch.arena;
    String8* symbol_names = BUSTER_ARENA_PUSH_ARRAY(work, String8, definition_capacity + 1);
    uint32_t* symbol_scopes = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, definition_capacity + 1);
    uint32_t* symbol_first = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, definition_capacity + 1);
    uint32_t* symbol_last = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, definition_capacity + 1);
    uint32_t* symbol_strong_count = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, definition_capacity + 1);
    uint32_t* slots = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, slot_count);
    uint8_t* definition_kinds = BUSTER_ARENA_PUSH_ARRAY(work, uint8_t, definition_capacity + 1);
    uint32_t* definition_indices = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, definition_capacity + 1);
    uint32_t* definition_next = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, definition_capacity + 1);
    bool* definition_strong = BUSTER_ARENA_PUSH_ARRAY(work, bool, definition_capacity + 1);
    String8* names = BUSTER_ARENA_PUSH_ARRAY(work, String8, function_capacity + 1);
    uint32_t* units = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* alias_units = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, alias_capacity + 1);
    String8* alias_targets = BUSTER_ARENA_PUSH_ARRAY(work, String8, alias_capacity + 1);
    uint64_t* alias_stamps = BUSTER_ARENA_PUSH_ARRAY(work, uint64_t, alias_capacity + 1);
    uint32_t* work_units = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, alias_capacity + 1);
    String8* work_names = BUSTER_ARENA_PUSH_ARRAY(work, String8, alias_capacity + 1);
    uint32_t* call_callers = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, call_capacity + 1);
    uint32_t* call_units = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, call_capacity + 1);
    String8* call_names = BUSTER_ARENA_PUSH_ARRAY(work, String8, call_capacity + 1);
    uint64_t* call_lines = BUSTER_ARENA_PUSH_ARRAY(work, uint64_t, call_capacity + 1);
    uint32_t* callers_begin = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 2);
    bool* calls_itself = BUSTER_ARENA_PUSH_ARRAY(work, bool, function_capacity + 1);
    // Tarjan's algorithm: each function's visit index and lowest reachable index, the component stack, the walk's own
    // stack of functions with the next call of each to follow, and each function's component.
    uint32_t* visit = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* lowest = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    bool* on_stack = BUSTER_ARENA_PUSH_ARRAY(work, bool, function_capacity + 1);
    uint32_t* component_stack = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* walk_functions = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* walk_calls = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* components = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    // The functions grouped by component, each component's first function, and the breadth-first search for a
    // closing path: its queue, each function's predecessor and the search that last reached it, and the path.
    uint32_t* component_begin = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 2);
    uint32_t* members = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* component_first = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* queue = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* predecessors = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* reached = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    uint32_t* path = BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, function_capacity + 1);
    bool* allowed_used = BUSTER_ARENA_PUSH_ARRAY(work, bool, allowed_count + 1);
    bool memory = fits && symbol_names != 0 && symbol_scopes != 0 && symbol_first != 0 && symbol_last != 0 && symbol_strong_count != 0 && slots != 0 &&
                  definition_kinds != 0 && definition_indices != 0 && definition_next != 0 && definition_strong != 0 && names != 0 && units != 0 && alias_units != 0 &&
                  alias_targets != 0 && alias_stamps != 0 && work_units != 0 && work_names != 0 && call_callers != 0 && call_units != 0 && call_names != 0 &&
                  call_lines != 0 && callers_begin != 0 && calls_itself != 0 && visit != 0 && lowest != 0 && on_stack != 0 && component_stack != 0 &&
                  walk_functions != 0 && walk_calls != 0 && components != 0 && component_begin != 0 && members != 0 && component_first != 0 && queue != 0 &&
                  predecessors != 0 && reached != 0 && path != 0 && allowed_used != 0;

    CallGraphReport report = {
        .arena = arena,
        .begin = (char*)arena->base + arena->length,
        .length = 0,
        .full = false,
    };
    uint32_t function_count = 0;
    uint32_t call_count = 0;
    uint32_t component_count = 0;
    uint32_t* targets = 0;
    if (memory)
    {
        memset(slots, 0, slot_count * sizeof(*slots));
        memset(allowed_used, 0, (allowed_count + 1) * sizeof(*allowed_used));
        CallGraphSymbols symbols = {
            .table = {
                .slots = slots,
                .mask = slot_count - 1,
                .names = symbol_names,
                .scopes = symbol_scopes,
            },
            .first = symbol_first,
            .strong_count = symbol_strong_count,
            .kinds = definition_kinds,
            .indices = definition_indices,
            .next = definition_next,
            .strong = definition_strong,
            .alias_units = alias_units,
            .alias_targets = alias_targets,
            .alias_stamps = alias_stamps,
            .work_units = work_units,
            .work_names = work_names,
        };

        // The definitions and their calls, and the aliases. Each definition is added to its symbol's list, in order;
        // every function definition is a function of the graph, even a weak one that a strong one replaces, and a
        // second strong definition of a symbol is a problem.
        uint32_t symbol_count = 0;
        uint32_t definition_count = 0;
        uint32_t alias_count = 0;
        uint32_t call_line_index = 0;
        for (uint32_t unit = 0; unit < input_count; unit += 1)
        {
            const String8 text = inputs[unit].text;
            uint64_t at = 0;
            uint64_t line_number = 1;
            uint32_t current = BUSTER_CALL_GRAPH_NONE;
            uint64_t current_line = 0;
            while (at < text.length)
            {
                const String8 line = call_graph_line(text, at);
                const CallGraphDefine define = call_graph_define(line);
                const CallGraphAlias alias = call_graph_alias(line);
                const CallGraphCall call = call_graph_call(line);
                if (define.is_define && current != BUSTER_CALL_GRAPH_NONE)
                {
                    call_graph_append_problem(&report, inputs[unit].name, current_line, BUSTER_S8("a definition without its closing brace"));
                    result.problem_count += 1;
                }
                if (define.is_define)
                {
                    current = BUSTER_CALL_GRAPH_NONE;
                }
                if ((define.is_define && define.malformed) || (alias.is_alias && alias.malformed))
                {
                    call_graph_append_problem(&report, inputs[unit].name, line_number, define.is_define ? BUSTER_S8("a definition without a name") : BUSTER_S8("an alias without a target"));
                    result.problem_count += 1;
                }
                else if (define.is_define || alias.is_alias)
                {
                    const CallGraphBinding binding = define.is_define ? define.binding : alias.binding;
                    const String8 name = define.is_define ? define.name : alias.name;
                    const uint32_t scope = binding == CALL_GRAPH_BINDING_LOCAL ? unit : BUSTER_CALL_GRAPH_EXTERNAL;
                    const uint64_t slot = call_graph_find(&symbols.table, scope, name);
                    if (slots[slot] == 0)
                    {
                        symbol_names[symbol_count] = name;
                        symbol_scopes[symbol_count] = scope;
                        symbol_first[symbol_count] = BUSTER_CALL_GRAPH_NONE;
                        symbol_strong_count[symbol_count] = 0;
                        symbol_count += 1;
                        slots[slot] = symbol_count;
                    }
                    const uint32_t symbol = slots[slot] - 1;
                    definition_strong[definition_count] = binding != CALL_GRAPH_BINDING_WEAK;
                    definition_next[definition_count] = BUSTER_CALL_GRAPH_NONE;
                    if (symbol_first[symbol] == BUSTER_CALL_GRAPH_NONE)
                    {
                        symbol_first[symbol] = definition_count;
                    }
                    else
                    {
                        definition_next[symbol_last[symbol]] = definition_count;
                    }
                    symbol_last[symbol] = definition_count;
                    if (definition_strong[definition_count] && symbol_strong_count[symbol] != 0)
                    {
                        call_graph_append_location(&report, inputs[unit].name, line_number);
                        call_graph_append(&report, BUSTER_S8("a second strong definition of "));
                        call_graph_append(&report, name);
                        call_graph_append(&report, BUSTER_S8("\n"));
                        result.problem_count += 1;
                    }
                    symbol_strong_count[symbol] += definition_strong[definition_count];
                    if (define.is_define)
                    {
                        definition_kinds[definition_count] = CALL_GRAPH_KIND_FUNCTION;
                        definition_indices[definition_count] = function_count;
                        names[function_count] = name;
                        units[function_count] = unit;
                        current = function_count;
                        current_line = line_number;
                        function_count += 1;
                    }
                    else
                    {
                        definition_kinds[definition_count] = alias.ifunc ? CALL_GRAPH_KIND_IFUNC : CALL_GRAPH_KIND_ALIAS;
                        definition_indices[definition_count] = alias_count;
                        alias_units[alias_count] = unit;
                        alias_targets[alias_count] = alias.target;
                        alias_stamps[alias_count] = UINT64_MAX;
                        alias_count += 1;
                    }
                    definition_count += 1;
                }
                else if (call_graph_equal(line, BUSTER_S8("}")))
                {
                    current = BUSTER_CALL_GRAPH_NONE;
                }
                else if (call.callee.length != 0 && current != BUSTER_CALL_GRAPH_NONE)
                {
                    call_callers[call_line_index] = current;
                    call_units[call_line_index] = unit;
                    call_names[call_line_index] = call.callee;
                    call_lines[call_line_index] = line_number;
                    call_line_index += 1;
                }
                at += line.length + 1;
                line_number += 1;
            }
            if (current != BUSTER_CALL_GRAPH_NONE)
            {
                call_graph_append_problem(&report, inputs[unit].name, current_line, BUSTER_S8("a definition without its closing brace"));
                result.problem_count += 1;
            }
        }

        // Each call's callees, which call_graph_resolve finds: counted first, then written after their caller's earlier
        // calls' (callers_begin[f + 2] counts f's; its prefix sums leave callers_begin[f + 1] where f's start, which
        // writing advances to where they end, f + 1's start).
        memset(callers_begin, 0, (function_count + 2) * sizeof(*callers_begin));
        memset(calls_itself, 0, function_count * sizeof(*calls_itself));
        uint64_t edge_count = 0;
        for (uint32_t i = 0; i < call_line_index; i += 1)
        {
            const CallGraphCallees callees = call_graph_resolve(&symbols, call_units[i], call_names[i], 2 * (uint64_t)i, 0);
            if (callees.ifunc || callees.lost)
            {
                call_graph_append_location(&report, inputs[call_units[i]].name, call_lines[i]);
                call_graph_append(&report, BUSTER_S8("a call of "));
                call_graph_append(&report, call_names[i]);
                call_graph_append(&report, callees.ifunc ? BUSTER_S8(", an ifunc, whose callee is chosen at run time\n") : BUSTER_S8(", an alias that leads to no definition\n"));
                result.problem_count += 1;
            }
            callers_begin[call_callers[i] + 2] += callees.count;
            edge_count += callees.count;
        }
        memory = edge_count < UINT32_MAX;
        targets = memory ? BUSTER_ARENA_PUSH_ARRAY(work, uint32_t, edge_count + 1) : 0;
        memory = targets != 0;
        call_count = memory ? (uint32_t)edge_count : 0;
        for (uint32_t f = 0; memory && f < function_count; f += 1)
        {
            callers_begin[f + 2] += callers_begin[f + 1];
        }
        for (uint32_t i = 0; memory && i < call_line_index; i += 1)
        {
            const uint32_t caller = call_callers[i];
            uint32_t* written = targets + callers_begin[caller + 1];
            const CallGraphCallees callees = call_graph_resolve(&symbols, call_units[i], call_names[i], 2 * (uint64_t)i + 1, written);
            for (uint32_t c = 0; c < callees.count; c += 1)
            {
                calls_itself[caller] = calls_itself[caller] || written[c] == caller;
            }
            callers_begin[caller + 1] += callees.count;
        }
    }
    if (memory)
    {
        // Tarjan's algorithm, with the walk's stack in walk_functions and walk_calls instead of recursion.
        for (uint32_t f = 0; f < function_count; f += 1)
        {
            visit[f] = BUSTER_CALL_GRAPH_NONE;
            on_stack[f] = false;
        }
        uint32_t visits = 0;
        uint32_t component_depth = 0;
        for (uint32_t root = 0; root < function_count; root += 1)
        {
            uint32_t walk_depth = 0;
            if (visit[root] == BUSTER_CALL_GRAPH_NONE)
            {
                visit[root] = visits;
                lowest[root] = visits;
                visits += 1;
                component_stack[component_depth] = root;
                component_depth += 1;
                on_stack[root] = true;
                walk_functions[0] = root;
                walk_calls[0] = callers_begin[root];
                walk_depth = 1;
            }
            while (walk_depth != 0)
            {
                const uint32_t function = walk_functions[walk_depth - 1];
                const uint32_t next = walk_calls[walk_depth - 1];
                if (next < callers_begin[function + 1])
                {
                    walk_calls[walk_depth - 1] = next + 1;
                    const uint32_t callee = targets[next];
                    if (visit[callee] == BUSTER_CALL_GRAPH_NONE)
                    {
                        visit[callee] = visits;
                        lowest[callee] = visits;
                        visits += 1;
                        component_stack[component_depth] = callee;
                        component_depth += 1;
                        on_stack[callee] = true;
                        walk_functions[walk_depth] = callee;
                        walk_calls[walk_depth] = callers_begin[callee];
                        walk_depth += 1;
                    }
                    else if (on_stack[callee] && visit[callee] < lowest[function])
                    {
                        lowest[function] = visit[callee];
                    }
                }
                else
                {
                    walk_depth -= 1;
                    if (walk_depth != 0 && lowest[function] < lowest[walk_functions[walk_depth - 1]])
                    {
                        lowest[walk_functions[walk_depth - 1]] = lowest[function];
                    }
                    if (lowest[function] == visit[function])
                    {
                        uint32_t member = BUSTER_CALL_GRAPH_NONE;
                        while (member != function)
                        {
                            component_depth -= 1;
                            member = component_stack[component_depth];
                            on_stack[member] = false;
                            components[member] = component_count;
                        }
                        component_count += 1;
                    }
                }
            }
        }

        // The functions by component, in the order they were defined; a component's first member is its first function.
        memset(component_begin, 0, (component_count + 2) * sizeof(*component_begin));
        for (uint32_t f = 0; f < function_count; f += 1)
        {
            component_begin[components[f] + 2] += 1;
        }
        for (uint32_t c = 0; c < component_count; c += 1)
        {
            component_begin[c + 2] += component_begin[c + 1];
        }
        for (uint32_t f = 0; f < function_count; f += 1)
        {
            members[component_begin[components[f] + 1]] = f;
            component_begin[components[f] + 1] += 1;
        }
        for (uint32_t c = 0; c < component_count; c += 1)
        {
            component_first[c] = members[component_begin[c]];
        }

        for (uint32_t f = 0; f < function_count; f += 1)
        {
            reached[f] = BUSTER_CALL_GRAPH_NONE;
        }
        for (uint32_t first = 0; first < function_count; first += 1)
        {
            const uint32_t component = components[first];
            const uint32_t size = component_begin[component + 1] - component_begin[component];
            if (component_first[component] == first && (size > 1 || calls_itself[first]))
            {
                uint64_t entry = allowed_count;
                for (uint64_t a = 0; a < allowed_count; a += 1)
                {
                    bool matches = call_graph_allowed_count(allowed[a].functions) == size;
                    for (uint32_t m = component_begin[component]; matches && m < component_begin[component + 1]; m += 1)
                    {
                        uint64_t occurrences = 0;
                        for (uint32_t other = component_begin[component]; other < component_begin[component + 1]; other += 1)
                        {
                            occurrences += call_graph_equal(names[members[other]], names[members[m]]);
                        }
                        matches = call_graph_allowed_occurrences(allowed[a].functions, names[members[m]]) == occurrences;
                    }
                    entry = matches && entry == allowed_count ? a : entry;
                }

                // The path: one function, or the shortest path from the first function back to it, which the
                // breadth-first search finds at the first function it reaches that calls the first.
                uint32_t path_length = 1;
                path[0] = first;
                if (size > 1)
                {
                    uint32_t head = 0;
                    uint32_t tail = 1;
                    uint32_t last = BUSTER_CALL_GRAPH_NONE;
                    queue[0] = first;
                    reached[first] = first;
                    while (last == BUSTER_CALL_GRAPH_NONE)
                    {
                        assert(head < tail);
                        const uint32_t function = queue[head];
                        head += 1;
                        for (uint32_t i = callers_begin[function]; last == BUSTER_CALL_GRAPH_NONE && i < callers_begin[function + 1]; i += 1)
                        {
                            const uint32_t callee = targets[i];
                            if (callee == first)
                            {
                                last = function;
                            }
                            else if (components[callee] == component && reached[callee] != first)
                            {
                                reached[callee] = first;
                                predecessors[callee] = function;
                                queue[tail] = callee;
                                tail += 1;
                            }
                        }
                    }
                    path_length = 0;
                    for (uint32_t function = last; function != first; function = predecessors[function])
                    {
                        path[path_length] = function;
                        path_length += 1;
                    }
                    path[path_length] = first;
                    path_length += 1;
                    for (uint32_t i = 0; i < path_length / 2; i += 1)
                    {
                        const uint32_t swap = path[i];
                        path[i] = path[path_length - 1 - i];
                        path[path_length - 1 - i] = swap;
                    }
                }

                call_graph_append(&report, entry != allowed_count ? BUSTER_S8("allowed cycle: ") : BUSTER_S8("cycle: "));
                for (uint32_t i = 0; i < path_length; i += 1)
                {
                    call_graph_append_function(&report, inputs, units, names, path[i]);
                    call_graph_append(&report, BUSTER_S8(" -> "));
                }
                call_graph_append_function(&report, inputs, units, names, first);
                call_graph_append(&report, BUSTER_S8("\n"));
                if (entry != allowed_count)
                {
                    allowed_used[entry] = true;
                    result.allowed_count += 1;
                }
                else
                {
                    result.cycle_count += 1;
                }
            }
        }

        for (uint64_t a = 0; a < allowed_count; a += 1)
        {
            if (!allowed_used[a])
            {
                call_graph_append(&report, BUSTER_S8("allowlist entry without a cycle: "));
                call_graph_append(&report, allowed[a].functions);
                call_graph_append(&report, BUSTER_S8("\n"));
                result.unused_allowed_count += 1;
            }
        }
    }

    call_graph_append(&report, BUSTER_S8("call graph: "));
    call_graph_append_count(&report, input_count, BUSTER_S8("translation unit, "), BUSTER_S8("translation units, "));
    call_graph_append_count(&report, function_count, BUSTER_S8("function, "), BUSTER_S8("functions, "));
    call_graph_append_count(&report, call_count, BUSTER_S8("call between them, "), BUSTER_S8("calls between them, "));
    call_graph_append_count(&report, result.cycle_count, BUSTER_S8("cycle, "), BUSTER_S8("cycles, "));
    call_graph_append_number(&report, result.allowed_count);
    call_graph_append(&report, BUSTER_S8(" allowed\n"));

    result.report = (String8){ .pointer = report.begin, .length = report.length };
    result.function_count = function_count;
    result.call_count = call_count;
    scratch_end(scratch);
    result.full = report.full || !memory;
    result.success = !result.full && result.problem_count == 0 && result.cycle_count == 0 && result.unused_allowed_count == 0;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

// The arena holds the files and everything call_graph_check pushes; its pages are committed only as they are touched.
#define BUSTER_CALL_GRAPH_ARENA_CAPACITY (64ull << 30)

static int run_check_call_graph(int argc, char** argv)
{
    Arena arena = arena_reserve(BUSTER_CALL_GRAPH_ARENA_CAPACITY);
    CallGraphInput* inputs = BUSTER_ARENA_PUSH_ARRAY(&arena, CallGraphInput, (uint64_t)argc + 1);
    bool valid = argc > 0 && inputs != 0;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const int fd = open(argv[i], O_RDONLY | O_CLOEXEC);
        struct stat stats;
        char* bytes = 0;
        uint64_t length = 0;
        if (fd != -1 && fstat(fd, &stats) == 0)
        {
            length = (uint64_t)stats.st_size;
            bytes = arena_push(&arena, length + 1, 1);
        }
        uint64_t read_bytes = 0;
        bool reading = bytes != 0;
        while (reading && read_bytes < length)
        {
            const ssize_t count = read(fd, bytes + read_bytes, length - read_bytes);
            reading = count > 0;
            read_bytes += reading ? (uint64_t)count : 0;
        }
        if (fd != -1)
        {
            close(fd);
        }
        if (bytes == 0 || read_bytes != length)
        {
            fprintf(stderr, "check-call-graph: %s couldn't be read\n", argv[i]);
            valid = false;
        }
        else
        {
            inputs[i] = (CallGraphInput){
                .name = { .pointer = argv[i], .length = strlen(argv[i]) },
                .text = { .pointer = bytes, .length = length },
            };
        }
    }

    int result = 1;
    if (argc == 0)
    {
        fprintf(stderr, "usage: check-call-graph FILE.ll...\n");
    }
    else if (valid)
    {
        const CallGraphResult check = call_graph_check(&arena, inputs, (uint32_t)argc, call_graph_allowed, BUSTER_ARRAY_LENGTH(call_graph_allowed));
        fwrite(check.report.pointer, 1, check.report.length, stdout);
        if (check.full)
        {
            fprintf(stderr, "check-call-graph: out of memory\n");
        }
        result = check.success ? 0 : 1;
    }
    arena_release(&arena);
    return result;
}
