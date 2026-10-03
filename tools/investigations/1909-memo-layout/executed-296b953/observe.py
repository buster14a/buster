"""Ephemeral source observer; exact replacement counts reject source drift."""
import pathlib
import sys

source = pathlib.Path(sys.argv[1])
text = source.read_text()
logger = r'''
#include <stdio.h>
#include <stdlib.h>
static FILE* memo_research_file;
static void memo_research_begin(unsigned count, unsigned start, unsigned scopes, unsigned types)
{
    if (!memo_research_file)
    {
        char const* path = getenv("BUSTER_MEMO_TRACE");
        if (path) memo_research_file = fopen(path, "w");
    }
    if (memo_research_file) fprintf(memo_research_file, "B %u %u %u %u\n", count, start, scopes, types);
}
static void memo_research_probe(CParseExpressionQuery const* query, unsigned reader, unsigned slot,
                                unsigned end, unsigned scope, unsigned flags, unsigned types)
{
    if (memo_research_file && query)
    {
        unsigned hit = query->end == end && query->scope.value == scope &&
            (query->flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) == (flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) &&
            (query->flags & flags) == flags && query->type.value < types;
        fprintf(memo_research_file, "Q %u %u %u %u %u %u %u\n", reader, slot, end, scope, flags, types, hit);
    }
}
static void memo_research_write(unsigned slot, unsigned end, unsigned scope, unsigned type, unsigned flags)
{
    if (memo_research_file) fprintf(memo_research_file, "W %u %u %u %u %u\n", slot, end, scope, type, flags);
}
static void memo_research_end(void)
{
    if (memo_research_file) fputs("E\n", memo_research_file);
}
'''

def replace(old, new, count=1):
    global text
    assert text.count(old) == count, (old[:100], text.count(old), count)
    text = text.replace(old, new)

replace('BUSTER_C_INTERNAL bool c_parse_expression_type_query(', 'BUSTER_C_INTERNAL bool c_parse_expression_type_query(', 1)
# Insert after normal headers, before first function that uses the memo.
anchor = 'BUSTER_GLOBAL_LOCAL'
offset = text.index(anchor)
text = text[:offset] + logger + text[offset:]
replace('            if (query && query->end == task->end && query->scope.value == scope.value &&',
        '            memo_research_probe(query, 0, task->start - machine->expression_query_start, task->end, scope.value, flags, result->type_count);\n            if (query && query->end == task->end && query->scope.value == scope.value &&')
replace('    if (query && query->end == end && query->scope.value == scope.value &&',
        '    memo_research_probe(query, 1, start - machine->expression_query_start, end, scope.value, flags, result->type_count);\n    if (query && query->end == end && query->scope.value == scope.value &&')
replace('        if (query && valid && !machine->expression_constraint.length && result->diagnostic_count == checkpoint.diagnostic_count)\n            *query = (CParseExpressionQuery){.end = end, .scope = scope, .type = *type_out, .flags = flags};',
        '        if (query && valid && !machine->expression_constraint.length && result->diagnostic_count == checkpoint.diagnostic_count)\n        {\n            memo_research_write(start - machine->expression_query_start, end, scope.value, type_out->value, flags);\n            *query = (CParseExpressionQuery){.end = end, .scope = scope, .type = *type_out, .flags = flags};\n        }')
replace('        machine->expression_queries = arena_allocate(machine->scratch_arena, CParseExpressionQuery, declaration->body_token_count);',
        '        memo_research_begin(declaration->body_token_count, declaration->body_start, result->scope_count, result->type_count);\n        machine->expression_queries = arena_allocate(machine->scratch_arena, CParseExpressionQuery, declaration->body_token_count);')
replace('        machine->expression_queries = 0;', '        memo_research_end();\n        machine->expression_queries = 0;')
source.write_text(text)
