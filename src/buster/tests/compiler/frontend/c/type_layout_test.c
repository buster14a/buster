// Tests for the parse-side layout solve (c_parse_type_layout_core in
// c_parse.c) and its two drivers. The ordered passes are the oracle: every
// fixture asks each question of both and requires the same answer, and the
// demand-driven agenda's work is pinned exactly on shapes whose closure is
// known, so a regression toward whole-table work fails by count, not timing.
// Map: c_type_layout_test_query / _sweep (the differential), the exact-count
// families (stable region, chain, fan-out, diamond, cycle), the fallback
// cases, and the seeded random programs (c_type_layout_test_program).
#include <buster/tests/compiler/frontend/c/type_layout_test.h>

#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/frontend/c/c_parse_internal.h>
#include <buster/lib/string.h>
#include <buster/lib/target.h>

#if BUSTER_INCLUDE_TESTS

#define C_TYPE_LAYOUT_TEST_RANDOM_PROGRAMS 160u

typedef struct CTypeLayoutTestText CTypeLayoutTestText;
struct CTypeLayoutTestText
{
    Arena* arena;
    char8* pointer;
    u64 length;
    u64 capacity;
};

BUSTER_GLOBAL_LOCAL void c_type_layout_test_append(CTypeLayoutTestText* text, String8 piece)
{
    if (text->length + piece.length > text->capacity)
    {
        u64 capacity = BUSTER_MAX(text->capacity * 2, text->length + piece.length);
        char8* pointer = arena_allocate(text->arena, char8, capacity);
        if (text->length)
        {
            memcpy(pointer, text->pointer, text->length);
        }
        text->pointer = pointer;
        text->capacity = capacity;
    }
    if (piece.length)
    {
        memcpy(text->pointer + text->length, piece.pointer, piece.length);
        text->length += piece.length;
    }
}

BUSTER_GLOBAL_LOCAL String8 c_type_layout_test_string(CTypeLayoutTestText* text)
{
    return (String8){.pointer = text->pointer, .length = text->length};
}

typedef struct CTypeLayoutTestUnit CTypeLayoutTestUnit;
struct CTypeLayoutTestUnit
{
    CPreprocessResult preprocess;
    CParseResult parse;
};

// One fixed LP64 target, so the pinned sizes and counts do not follow the host.
BUSTER_GLOBAL_LOCAL CTypeLayoutTestUnit c_type_layout_test_parse(Arena* arena, String8 source)
{
    TargetParseResult target = target_parse_triple(S8("x86_64-unknown-linux-gnu"));
    CTypeLayoutTestUnit unit = {0};
    unit.preprocess = c_preprocess(arena, source, (CPreprocessOptions){.target = target.target, .data_layout = target_data_layout(target.target)});
    unit.parse = c_parse(arena, unit.preprocess);
    return unit;
}

BUSTER_GLOBAL_LOCAL CTypeId c_type_layout_test_tag(CParseResult* parse, CTypeKind kind, String8 tag)
{
    CTypeId result = C_TYPE_ID_INVALID;
    for (u32 index = 0; index < parse->type_count && result.value == C_ID_UNDERLYING_INVALID; index += 1)
    {
        CType* type = parse->types + index;
        if (type->kind == kind && !type->has_unqualified_type && string_equal(type->tag, tag))
        {
            result = (CTypeId){.value = index};
        }
    }
    return result;
}

typedef struct CTypeLayoutTestAnswer CTypeLayoutTestAnswer;
struct CTypeLayoutTestAnswer
{
    CTypeLayoutStatistics statistics;
    u64 size;
    u64 offset;
    u32 alignment;
    bool resolved;
    u8 reserved[3];
};

// One query through the chosen driver; its scratch is rewound afterwards, so
// a sweep over a table holds one query's state at a time.
BUSTER_GLOBAL_LOCAL CTypeLayoutTestAnswer c_type_layout_test_query(Arena* arena, CTypeLayoutTestUnit* unit, CTypeId type, bool agenda, u32 offset_member)
{
    CTypeLayoutTestAnswer answer = {0};
    u64 position = arena->position;
    answer.resolved = c_test_type_layout(arena, unit->preprocess, &unit->parse, type, agenda, offset_member, &answer.statistics, &answer.size,
                                         &answer.alignment, offset_member == UINT32_MAX ? 0 : &answer.offset);
    arena_set_position(arena, position);
    return answer;
}

BUSTER_GLOBAL_LOCAL bool c_type_layout_test_same(CTypeLayoutTestAnswer agenda, CTypeLayoutTestAnswer passes, bool offset)
{
    return agenda.resolved == passes.resolved &&
           (!agenda.resolved || (agenda.size == passes.size && agenda.alignment == passes.alignment && (!offset || agenda.offset == passes.offset)));
}

typedef struct CTypeLayoutTestSweep CTypeLayoutTestSweep;
struct CTypeLayoutTestSweep
{
    CTypeLayoutStatistics agenda;
    CTypeLayoutStatistics passes;
    u64 queries;
    u64 offset_queries;
    u64 mismatches;
    u64 resolved;
    // Agenda queries whose distinct types exceeded the query's own table,
    // which a closure can never do.
    u64 closure_overruns;
    // Agenda queries that attempted an entry they never pushed.
    u64 unpushed_attempts;
    u32 first_mismatch;
    u8 reserved[4];
};

BUSTER_GLOBAL_LOCAL void c_type_layout_test_statistics_add(CTypeLayoutStatistics* total, CTypeLayoutStatistics part)
{
    total->solves += part.solves;
    total->pass_solves += part.pass_solves;
    total->pass_state_types += part.pass_state_types;
    total->pass_attempts += part.pass_attempts;
    total->agenda_solves += part.agenda_solves;
    total->agenda_types += part.agenda_types;
    total->agenda_attempts += part.agenda_attempts;
    total->agenda_edges += part.agenda_edges;
    total->agenda_notifications += part.agenda_notifications;
    total->agenda_pushes += part.agenda_pushes;
    total->agenda_fallbacks += part.agenda_fallbacks;
}

BUSTER_GLOBAL_LOCAL void c_type_layout_test_record(CTypeLayoutTestSweep* sweep, CTypeLayoutTestAnswer agenda, CTypeLayoutTestAnswer passes, u32 type_count,
                                                    bool offset, u32 type_index)
{
    bool same = c_type_layout_test_same(agenda, passes, offset);
    sweep->queries += !offset;
    sweep->offset_queries += offset;
    sweep->mismatches += !same;
    sweep->resolved += agenda.resolved;
    sweep->closure_overruns += agenda.statistics.agenda_types > type_count;
    sweep->unpushed_attempts += agenda.statistics.agenda_attempts > agenda.statistics.agenda_pushes;
    if (!same && sweep->first_mismatch == UINT32_MAX)
    {
        sweep->first_mismatch = type_index;
    }
    c_type_layout_test_statistics_add(&sweep->agenda, agenda.statistics);
    c_type_layout_test_statistics_add(&sweep->passes, passes.statistics);
}

// Every type of the unit, and every member offset of every complete
// aggregate, asked of both drivers.
BUSTER_GLOBAL_LOCAL CTypeLayoutTestSweep c_type_layout_test_sweep(Arena* arena, CTypeLayoutTestUnit* unit)
{
    CTypeLayoutTestSweep sweep = {.first_mismatch = UINT32_MAX};
    u32 type_count = unit->parse.type_count;
    for (u32 type_index = 0; type_index < type_count; type_index += 1)
    {
        CTypeId type = {.value = type_index};
        c_type_layout_test_record(&sweep, c_type_layout_test_query(arena, unit, type, true, UINT32_MAX), c_type_layout_test_query(arena, unit, type, false, UINT32_MAX),
                                  type_count, false, type_index);
        CType record = unit->parse.types[type_index];
        if ((record.kind == C_TYPE_STRUCT || record.kind == C_TYPE_UNION) && record.is_complete)
        {
            for (u32 member_index = 0; member_index < record.member_count; member_index += 1)
            {
                u32 offset_member = record.member_start + member_index;
                c_type_layout_test_record(&sweep, c_type_layout_test_query(arena, unit, type, true, offset_member),
                                          c_type_layout_test_query(arena, unit, type, false, offset_member), type_count, true, type_index);
            }
        }
    }
    return sweep;
}

BUSTER_GLOBAL_LOCAL bool c_type_layout_test_agenda_work(CTypeLayoutStatistics statistics, u64 types, u64 attempts, u64 edges, u64 notifications, u64 pushes)
{
    return statistics.solves == 1 && statistics.agenda_solves == 1 && statistics.pass_solves == 0 && statistics.agenda_fallbacks == 0 &&
           statistics.agenda_types == types && statistics.agenda_attempts == attempts && statistics.agenda_edges == edges &&
           statistics.agenda_notifications == notifications && statistics.agenda_pushes == pushes;
}

// A large stable region and one small question about a type outside it. The
// agenda reaches the requested struct and its two seeded members whatever the
// region's size; the ordered passes build per-query state for, and attempt,
// the whole region every time.
BUSTER_GLOBAL_LOCAL UnitTestResult c_type_layout_test_stable_region(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 const region_sizes[] = {0, 64, 128, 1024};
    u64 state_types[BUSTER_ARRAY_LENGTH(region_sizes)] = {0};
    u64 pass_attempts[BUSTER_ARRAY_LENGTH(region_sizes)] = {0};
    for (u32 size_index = 0; size_index < BUSTER_ARRAY_LENGTH(region_sizes); size_index += 1)
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CTypeLayoutTestText text = {.arena = temporary.arena};
        for (u32 index = 0; index < region_sizes[size_index]; index += 1)
        {
            c_type_layout_test_append(&text, string_format(temporary.arena, S8("struct U{u32} {{ int a; char b[3]; };\n"), index));
        }
        c_type_layout_test_append(&text, S8("struct Q { int q; long r; };\n"));
        CTypeLayoutTestUnit unit = c_type_layout_test_parse(temporary.arena, c_type_layout_test_string(&text));
        BUSTER_TEST(arguments, unit.parse.diagnostic_count == 0);
        CTypeId question = c_type_layout_test_tag(&unit.parse, C_TYPE_STRUCT, S8("Q"));
        if (BUSTER_REQUIRE(arguments, question.value != C_ID_UNDERLYING_INVALID))
        {
            CTypeLayoutTestAnswer agenda = c_type_layout_test_query(temporary.arena, &unit, question, true, UINT32_MAX);
            CTypeLayoutTestAnswer passes = c_type_layout_test_query(temporary.arena, &unit, question, false, UINT32_MAX);
            BUSTER_TEST(arguments, c_type_layout_test_same(agenda, passes, false));
            BUSTER_TEST(arguments, agenda.resolved && agenda.size == 16 && agenda.alignment == 8);
            BUSTER_TEST(arguments, c_type_layout_test_agenda_work(agenda.statistics, 3, 1, 0, 0, 1));
            BUSTER_TEST(arguments, passes.statistics.pass_solves == 1 && passes.statistics.agenda_solves == 0);
            BUSTER_TEST(arguments, passes.statistics.pass_state_types == unit.parse.type_count);
            state_types[size_index] = passes.statistics.pass_state_types;
            pass_attempts[size_index] = passes.statistics.pass_attempts;
        }
        scratch_end(temporary);
    }
    // Past the first region struct, which may create types the others share,
    // the ordered passes grow by the same amount per region struct: exactly
    // linear in the table, where the agenda above is constant.
    u64 state_step = (state_types[2] - state_types[1]) / (region_sizes[2] - region_sizes[1]);
    u64 attempt_step = (pass_attempts[2] - pass_attempts[1]) / (region_sizes[2] - region_sizes[1]);
    BUSTER_TEST(arguments, state_step && state_types[2] - state_types[1] == state_step * (region_sizes[2] - region_sizes[1]));
    BUSTER_TEST(arguments, state_types[3] - state_types[1] == state_step * (region_sizes[3] - region_sizes[1]));
    BUSTER_TEST(arguments, attempt_step && pass_attempts[2] - pass_attempts[1] == attempt_step * (region_sizes[2] - region_sizes[1]));
    BUSTER_TEST(arguments, pass_attempts[3] - pass_attempts[1] == attempt_step * (region_sizes[3] - region_sizes[1]));
    return result;
}

// A containment chain S0 <- S1 <- ... <- SD, asked at its top. Each link is
// attempted once optimistically, blocked on the link below, and once more
// after that link finishes; S0 resolves at its first attempt. Each blocked
// link registers its blocker twice (as the blocker and again as a static
// prerequisite), so each finish notifies two edges. Every declaration's
// `int` is a type record of its own, seeded on first read, so the agenda
// enters D + 1 links and D + 1 ints.
BUSTER_GLOBAL_LOCAL UnitTestResult c_type_layout_test_chain(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 const depths[] = {1, 8, 64};
    for (u32 depth_index = 0; depth_index < BUSTER_ARRAY_LENGTH(depths); depth_index += 1)
    {
        u32 depth = depths[depth_index];
        TemporalArena temporary = scratch_begin(0, 0);
        CTypeLayoutTestText text = {.arena = temporary.arena};
        c_type_layout_test_append(&text, S8("struct S0 { int x; };\n"));
        for (u32 link = 1; link <= depth; link += 1)
        {
            c_type_layout_test_append(&text, string_format(temporary.arena, S8("struct S{u32} {{ struct S{u32} inner; int x; };\n"), link, link - 1));
        }
        CTypeLayoutTestUnit unit = c_type_layout_test_parse(temporary.arena, c_type_layout_test_string(&text));
        BUSTER_TEST(arguments, unit.parse.diagnostic_count == 0);
        CTypeId top = c_type_layout_test_tag(&unit.parse, C_TYPE_STRUCT, string_format(temporary.arena, S8("S{u32}"), depth));
        if (BUSTER_REQUIRE(arguments, top.value != C_ID_UNDERLYING_INVALID))
        {
            CTypeLayoutTestAnswer agenda = c_type_layout_test_query(temporary.arena, &unit, top, true, UINT32_MAX);
            CTypeLayoutTestAnswer passes = c_type_layout_test_query(temporary.arena, &unit, top, false, UINT32_MAX);
            BUSTER_TEST(arguments, c_type_layout_test_same(agenda, passes, false));
            BUSTER_TEST(arguments, agenda.resolved && agenda.size == 4ull * (depth + 1) && agenda.alignment == 4);
            BUSTER_TEST(arguments,
                        c_type_layout_test_agenda_work(agenda.statistics, 2ull * depth + 2, 2ull * depth + 1, 2ull * depth, 2ull * depth, 2ull * depth + 1));
        }
        scratch_end(temporary);
    }
    return result;
}

// One aggregate over W distinct leaf structs. The first attempt blocks on the
// first leaf; the expansion registers every leaf, the first twice; every leaf
// resolves at its first attempt and the aggregate at its second. The agenda
// enters the aggregate, the W leaves and each leaf's own `int` record.
BUSTER_GLOBAL_LOCAL UnitTestResult c_type_layout_test_fan_out(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 const widths[] = {1, 16, 256};
    for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(widths); width_index += 1)
    {
        u32 width = widths[width_index];
        TemporalArena temporary = scratch_begin(0, 0);
        CTypeLayoutTestText text = {.arena = temporary.arena};
        for (u32 leaf = 0; leaf < width; leaf += 1)
        {
            c_type_layout_test_append(&text, string_format(temporary.arena, S8("struct L{u32} {{ int x; };\n"), leaf));
        }
        c_type_layout_test_append(&text, S8("struct F {\n"));
        for (u32 leaf = 0; leaf < width; leaf += 1)
        {
            c_type_layout_test_append(&text, string_format(temporary.arena, S8("    struct L{u32} m{u32};\n"), leaf, leaf));
        }
        c_type_layout_test_append(&text, S8("};\n"));
        CTypeLayoutTestUnit unit = c_type_layout_test_parse(temporary.arena, c_type_layout_test_string(&text));
        BUSTER_TEST(arguments, unit.parse.diagnostic_count == 0);
        CTypeId aggregate = c_type_layout_test_tag(&unit.parse, C_TYPE_STRUCT, S8("F"));
        if (BUSTER_REQUIRE(arguments, aggregate.value != C_ID_UNDERLYING_INVALID))
        {
            CTypeLayoutTestAnswer agenda = c_type_layout_test_query(temporary.arena, &unit, aggregate, true, UINT32_MAX);
            CTypeLayoutTestAnswer passes = c_type_layout_test_query(temporary.arena, &unit, aggregate, false, UINT32_MAX);
            BUSTER_TEST(arguments, c_type_layout_test_same(agenda, passes, false));
            BUSTER_TEST(arguments, agenda.resolved && agenda.size == 4ull * width && agenda.alignment == 4);
            BUSTER_TEST(arguments, c_type_layout_test_agenda_work(agenda.statistics, 2ull * width + 1, width + 2, width + 1, width + 1, width + 2));
        }
        scratch_end(temporary);
    }
    return result;
}

// Two paths to one base: the base is attempted once, and each of the two
// middle structs' finishes reaches the top through its own edges.
BUSTER_GLOBAL_LOCAL UnitTestResult c_type_layout_test_diamond(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(0, 0);
    CTypeLayoutTestUnit unit = c_type_layout_test_parse(temporary.arena, S8("struct B { int x; };\n"
                                                                           "struct L { struct B b; };\n"
                                                                           "struct R { struct B b; };\n"
                                                                           "struct T { struct L l; struct R r; };\n"));
    BUSTER_TEST(arguments, unit.parse.diagnostic_count == 0);
    CTypeId top = c_type_layout_test_tag(&unit.parse, C_TYPE_STRUCT, S8("T"));
    if (BUSTER_REQUIRE(arguments, top.value != C_ID_UNDERLYING_INVALID))
    {
        CTypeLayoutTestAnswer agenda = c_type_layout_test_query(temporary.arena, &unit, top, true, UINT32_MAX);
        CTypeLayoutTestAnswer passes = c_type_layout_test_query(temporary.arena, &unit, top, false, UINT32_MAX);
        BUSTER_TEST(arguments, c_type_layout_test_same(agenda, passes, false));
        BUSTER_TEST(arguments, agenda.resolved && agenda.size == 8 && agenda.alignment == 4);
        BUSTER_TEST(arguments, c_type_layout_test_agenda_work(agenda.statistics, 5, 6, 5, 5, 6));
    }
    scratch_end(temporary);
    return result;
}

// Invalid mutual containment and a bound that sizes its own struct: neither
// resolves under any order. The ordered passes stop on a pass without
// progress; the agenda stops when nothing is ready, every open type waiting on
// another through necessary conditions. Both answer "unresolved".
BUSTER_GLOBAL_LOCAL UnitTestResult c_type_layout_test_cycles(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 const sources[] = {
        S8("struct A;\nstruct B;\nstruct A { struct B b; int a; };\nstruct B { struct A a; int b; };\n"),
        S8("struct S { int x; char a[sizeof(struct S)]; };\n"),
        S8("struct P;\nstruct Q;\nstruct P { char p[sizeof(struct Q)]; };\nstruct Q { char q[sizeof(struct P)]; };\n"),
    };
    for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(sources); source_index += 1)
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CTypeLayoutTestUnit unit = c_type_layout_test_parse(temporary.arena, sources[source_index]);
        CTypeLayoutTestSweep sweep = c_type_layout_test_sweep(temporary.arena, &unit);
        BUSTER_TEST(arguments, sweep.queries == unit.parse.type_count);
        BUSTER_TEST(arguments, sweep.mismatches == 0);
        BUSTER_TEST(arguments, sweep.closure_overruns == 0 && sweep.unpushed_attempts == 0);
        BUSTER_TEST(arguments, sweep.agenda.agenda_fallbacks == 0);
        String8 const tags[] = {S8("A"), S8("S"), S8("P")};
        CTypeId cyclic = c_type_layout_test_tag(&unit.parse, C_TYPE_STRUCT, tags[source_index]);
        if (BUSTER_REQUIRE(arguments, cyclic.value != C_ID_UNDERLYING_INVALID))
        {
            CTypeLayoutTestAnswer agenda = c_type_layout_test_query(temporary.arena, &unit, cyclic, true, UINT32_MAX);
            BUSTER_TEST(arguments, !agenda.resolved);
            BUSTER_TEST(arguments, agenda.statistics.agenda_solves == 1 && agenda.statistics.agenda_fallbacks == 0);
            BUSTER_TEST(arguments, agenda.statistics.agenda_notifications == 0);
        }
        scratch_end(temporary);
    }
    return result;
}

// The one read whose answer depends on attempt order: a sizeof operand whose
// kind alone decides a layout. A seeded operand, or a kind whose first type is
// seeded, is answered by the agenda; a complete enum or an aligned alias is
// not seeded and hands the query to the ordered passes, with the same answer.
BUSTER_GLOBAL_LOCAL UnitTestResult c_type_layout_test_order_dependent_reads(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(0, 0);
    CTypeLayoutTestUnit unit = c_type_layout_test_parse(temporary.arena, S8("enum Color { RED, GREEN, BLUE };\n"
                                                                           "typedef int Wide __attribute__((aligned(16)));\n"
                                                                           "struct Seeded { char a[sizeof(int) + 1]; };\n"
                                                                           "struct Qualified { char a[sizeof(volatile unsigned short)]; };\n"
                                                                           "struct Enumerated { char a[sizeof(enum Color)]; };\n"
                                                                           "struct Aliased { char a[sizeof(Wide)]; };\n"));
    BUSTER_TEST(arguments, unit.parse.diagnostic_count == 0);
    String8 const tags[] = {S8("Seeded"), S8("Qualified"), S8("Enumerated"), S8("Aliased")};
    u64 const sizes[] = {5, 2, 4, 4};
    u64 const fallbacks[] = {0, 0, 1, 1};
    for (u32 tag_index = 0; tag_index < BUSTER_ARRAY_LENGTH(tags); tag_index += 1)
    {
        CTypeId type = c_type_layout_test_tag(&unit.parse, C_TYPE_STRUCT, tags[tag_index]);
        if (BUSTER_REQUIRE(arguments, type.value != C_ID_UNDERLYING_INVALID))
        {
            CTypeLayoutTestAnswer agenda = c_type_layout_test_query(temporary.arena, &unit, type, true, UINT32_MAX);
            CTypeLayoutTestAnswer passes = c_type_layout_test_query(temporary.arena, &unit, type, false, UINT32_MAX);
            BUSTER_TEST(arguments, c_type_layout_test_same(agenda, passes, false));
            BUSTER_TEST(arguments, agenda.resolved && agenda.size == sizes[tag_index]);
            BUSTER_TEST(arguments, agenda.statistics.agenda_fallbacks == fallbacks[tag_index]);
            BUSTER_TEST(arguments, agenda.statistics.pass_solves == fallbacks[tag_index]);
        }
    }
    CTypeLayoutTestSweep sweep = c_type_layout_test_sweep(temporary.arena, &unit);
    BUSTER_TEST(arguments, sweep.mismatches == 0 && sweep.closure_overruns == 0 && sweep.unpushed_attempts == 0);
    scratch_end(temporary);
    return result;
}

// The production entry: an enumerator's sizeof is a machineless, uncached
// query, which the agenda answers. Every fold must match the ordered passes
// asked the same question about the parsed table.
BUSTER_GLOBAL_LOCAL UnitTestResult c_type_layout_test_enumerator_folds(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(0, 0);
    CTypeLayoutTestText text = {.arena = temporary.arena};
    u32 const count = 48;
    for (u32 index = 0; index < count; index += 1)
    {
        c_type_layout_test_append(&text, string_format(temporary.arena,
                                                       S8("struct E{u32} {{ char c; long l[{u32}]; short s; }};\n"
                                                          "enum {{ SIZE{u32} = sizeof(struct E{u32}), ALIGN{u32} = _Alignof(struct E{u32}) };\n"),
                                                       index, index % 5 + 1, index, index, index, index));
    }
    CTypeLayoutTestUnit unit = c_type_layout_test_parse(temporary.arena, c_type_layout_test_string(&text));
    BUSTER_TEST(arguments, unit.parse.diagnostic_count == 0);
    CTypeLayoutStatistics production = unit.parse.type_layout_statistics ? *unit.parse.type_layout_statistics : (CTypeLayoutStatistics){0};
    BUSTER_TEST(arguments, production.agenda_solves >= count);
    BUSTER_TEST(arguments, production.agenda_fallbacks == 0);
    u32 checked = 0;
    for (u32 member_index = 0; member_index < unit.parse.enum_member_count; member_index += 1)
    {
        CEnumMember member = unit.parse.enum_members[member_index];
        bool size = member.name.length > 4 && string_equal(string_slice(member.name, 0, 4), S8("SIZE"));
        if (size)
        {
            u64 expected = 8 + 8 * ((u64)checked % 5 + 1) + 8;
            BUSTER_TEST(arguments, !member.is_negative && member.value == expected);
            checked += 1;
        }
    }
    BUSTER_TEST(arguments, checked == count);
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL u64 c_type_layout_test_random(u64* state)
{
    u64 value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

BUSTER_GLOBAL_LOCAL u32 c_type_layout_test_pick(u64* state, u32 bound)
{
    return (u32)(c_type_layout_test_random(state) % bound);
}

// A random program over the constructs the parse-side layout reads: struct
// and union containment in any definition order (so incomplete members and
// containment cycles occur), pointers, arrays bounded by literals, enumerators,
// arithmetic and sizeof of aggregates, scalars, enums and aligned aliases,
// bit-fields, _Atomic, member and aggregate alignment, packing, GNU vectors,
// flexible array members and enumerator sizeof folds.
BUSTER_GLOBAL_LOCAL String8 c_type_layout_test_program(Arena* arena, u64 seed)
{
    u64 state = seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
    CTypeLayoutTestText text = {.arena = arena};
    u32 aggregate_count = 2 + c_type_layout_test_pick(&state, 10);
    u32 enum_count = 1 + c_type_layout_test_pick(&state, 3);
    String8 const scalars[] = {S8("int"), S8("char"), S8("short"), S8("long"), S8("double"), S8("_Bool"), S8("unsigned"), S8("float"), S8("long long")};
    bool is_union[16] = {0};
    u32 order[16];
    for (u32 index = 0; index < aggregate_count; index += 1)
    {
        is_union[index] = c_type_layout_test_pick(&state, 4) == 0;
        order[index] = index;
    }
    for (u32 index = aggregate_count; index > 1; index -= 1)
    {
        u32 other = c_type_layout_test_pick(&state, index);
        u32 swap = order[index - 1];
        order[index - 1] = order[other];
        order[other] = swap;
    }
    for (u32 index = 0; index < enum_count; index += 1)
    {
        c_type_layout_test_append(&text, string_format(arena, S8("enum E{u32} {{ E{u32}_A = {u32}, E{u32}_B };\n"), index, index,
                                                       c_type_layout_test_pick(&state, 5) + 1, index));
    }
    c_type_layout_test_append(&text, S8("typedef int AlignedInt __attribute__((aligned(16)));\ntypedef char AlignedChar __attribute__((aligned(4)));\n"));
    for (u32 index = 0; index < aggregate_count; index += 1)
    {
        c_type_layout_test_append(&text, string_format(arena, S8("{S8} R{u32};\n"), is_union[index] ? S8("union") : S8("struct"), index));
    }
    for (u32 position = 0; position < aggregate_count; position += 1)
    {
        u32 index = order[position];
        u32 attribute = c_type_layout_test_pick(&state, 6);
        c_type_layout_test_append(&text, string_format(arena, S8("{S8} {S8}R{u32} {{\n"), is_union[index] ? S8("union") : S8("struct"),
                                                       attribute == 0 ? S8("__attribute__((packed)) ") : S8(""), index));
        u32 member_count = 1 + c_type_layout_test_pick(&state, 5);
        for (u32 member = 0; member < member_count; member += 1)
        {
            u32 other = c_type_layout_test_pick(&state, aggregate_count);
            String8 other_keyword = is_union[other] ? S8("union") : S8("struct");
            String8 scalar = scalars[c_type_layout_test_pick(&state, BUSTER_ARRAY_LENGTH(scalars))];
            String8 bound;
            switch (c_type_layout_test_pick(&state, 8))
            {
            case 0:
                bound = string_format(arena, S8("{u32}"), c_type_layout_test_pick(&state, 4) + 1);
                break;
            case 1:
                bound = string_format(arena, S8("E{u32}_B"), c_type_layout_test_pick(&state, enum_count));
                break;
            case 2:
                bound = string_format(arena, S8("sizeof({S8} R{u32})"), other_keyword, other);
                break;
            case 3:
                bound = S8("sizeof(int)");
                break;
            case 4:
                bound = S8("sizeof(AlignedInt)");
                break;
            case 5:
                bound = string_format(arena, S8("sizeof(enum E{u32})"), c_type_layout_test_pick(&state, enum_count));
                break;
            case 6:
                bound = S8("2 * 3");
                break;
            default:
                bound = S8("sizeof(volatile unsigned short)");
                break;
            }
            String8 line;
            switch (c_type_layout_test_pick(&state, 11))
            {
            case 0:
                line = string_format(arena, S8("    {S8} m{u32};\n"), scalar, member);
                break;
            case 1:
                line = string_format(arena, S8("    {S8} R{u32}* m{u32};\n"), other_keyword, other, member);
                break;
            case 2:
                line = string_format(arena, S8("    {S8} m{u32}[{S8}];\n"), scalar, member, bound);
                break;
            case 3:
                line = string_format(arena, S8("    {S8} R{u32} m{u32};\n"), other_keyword, other, member);
                break;
            case 4:
                line = string_format(arena, S8("    unsigned m{u32} : {u32};\n"), member, c_type_layout_test_pick(&state, 12) + 1);
                break;
            case 5:
                line = string_format(arena, S8("    _Atomic {S8} m{u32};\n"), c_type_layout_test_pick(&state, 2) ? S8("long") : S8("char"), member);
                break;
            case 6:
                line = string_format(arena, c_type_layout_test_pick(&state, 2) ? S8("    int m{u32} __attribute__((aligned(8)));\n")
                                                                               : S8("    _Alignas(16) char m{u32};\n"),
                                     member);
                break;
            case 7:
                line = string_format(arena, S8("    int m{u32} __attribute__((vector_size(16)));\n"), member);
                break;
            case 8:
                line = string_format(arena, S8("    enum E{u32} m{u32};\n"), c_type_layout_test_pick(&state, enum_count), member);
                break;
            case 9:
                line = string_format(arena, S8("    {S8} m{u32};\n"), c_type_layout_test_pick(&state, 2) ? S8("AlignedInt") : S8("AlignedChar"), member);
                break;
            default:
                line = string_format(arena, S8("    {S8} R{u32} m{u32}[{S8}];\n"), other_keyword, other, member, bound);
                break;
            }
            c_type_layout_test_append(&text, line);
        }
        if (!is_union[index] && member_count >= 2 && c_type_layout_test_pick(&state, 6) == 0)
        {
            c_type_layout_test_append(&text, S8("    char tail[];\n"));
        }
        c_type_layout_test_append(&text, attribute == 1 ? S8("} __attribute__((aligned(8)));\n") : S8("};\n"));
        if (c_type_layout_test_pick(&state, 2) == 0)
        {
            c_type_layout_test_append(&text, string_format(arena, S8("enum {{ Q{u32} = sizeof({S8} R{u32}) };\n"), index,
                                                           is_union[index] ? S8("union") : S8("struct"), index));
        }
    }
    c_type_layout_test_append(&text, S8("int main(void) { return 0; }\n"));
    return c_type_layout_test_string(&text);
}

// Seeded random programs, every type and every member offset asked of both
// drivers. Valid and invalid programs alike: an incomplete member, a
// containment cycle or an unfoldable bound must leave both unresolved.
BUSTER_GLOBAL_LOCAL UnitTestResult c_type_layout_test_random_programs(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    CTypeLayoutTestSweep total = {.first_mismatch = UINT32_MAX};
    u64 first_mismatching_seed = UINT64_MAX;
    for (u64 seed = 1; seed <= C_TYPE_LAYOUT_TEST_RANDOM_PROGRAMS; seed += 1)
    {
        TemporalArena temporary = scratch_begin(0, 0);
        String8 source = c_type_layout_test_program(temporary.arena, seed);
        CTypeLayoutTestUnit unit = c_type_layout_test_parse(temporary.arena, source);
        CTypeLayoutTestSweep sweep = c_type_layout_test_sweep(temporary.arena, &unit);
        if (sweep.mismatches && first_mismatching_seed == UINT64_MAX)
        {
            first_mismatching_seed = seed;
        }
        total.queries += sweep.queries;
        total.offset_queries += sweep.offset_queries;
        total.mismatches += sweep.mismatches;
        total.resolved += sweep.resolved;
        total.closure_overruns += sweep.closure_overruns;
        total.unpushed_attempts += sweep.unpushed_attempts;
        c_type_layout_test_statistics_add(&total.agenda, sweep.agenda);
        c_type_layout_test_statistics_add(&total.passes, sweep.passes);
        scratch_end(temporary);
    }
    BUSTER_TEST(arguments, first_mismatching_seed == UINT64_MAX);
    BUSTER_TEST(arguments, total.mismatches == 0);
    BUSTER_TEST(arguments, total.closure_overruns == 0 && total.unpushed_attempts == 0);
    // The corpus must reach every path: answered and unanswered queries,
    // blocked attempts, completed edges and fallbacks to the passes.
    BUSTER_TEST(arguments, total.resolved && total.resolved < total.queries + total.offset_queries);
    BUSTER_TEST(arguments, total.agenda.agenda_edges && total.agenda.agenda_notifications && total.agenda.agenda_fallbacks);
    BUSTER_TEST(arguments, total.agenda.agenda_solves > total.agenda.agenda_fallbacks);
    return result;
}

UnitTestResult c_type_layout_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, c_type_layout_test_stable_region);
    BUSTER_TEST_FIXTURE(arguments, c_type_layout_test_chain);
    BUSTER_TEST_FIXTURE(arguments, c_type_layout_test_fan_out);
    BUSTER_TEST_FIXTURE(arguments, c_type_layout_test_diamond);
    BUSTER_TEST_FIXTURE(arguments, c_type_layout_test_cycles);
    BUSTER_TEST_FIXTURE(arguments, c_type_layout_test_order_dependent_reads);
    BUSTER_TEST_FIXTURE(arguments, c_type_layout_test_enumerator_folds);
    BUSTER_TEST_FIXTURE(arguments, c_type_layout_test_random_programs);
    return result;
}

#endif
