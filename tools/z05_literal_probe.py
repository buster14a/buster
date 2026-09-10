#!/usr/bin/env python3
"""Untimed, serial-compiler-only Z05 call census for a frozen checkout.
Diagnostic source transform only: no replacement parser or timing harness.
"""
import argparse
import pathlib
import re

parser = argparse.ArgumentParser()
parser.add_argument('root', type=pathlib.Path)
args = parser.parse_args()
root = args.root / 'src/buster/lib/compiler/frontend/c'
files = {name: (root / name).read_text() for name in ('c_source.c', 'c_parse.c', 'c_gen.c', 'c_internal.h')}

def replace(name, old, new, count=1):
    assert files[name].count(old) == count, (name, old[:100], files[name].count(old))
    files[name] = files[name].replace(old, new)

def at_entry(name, symbol, statement):
    pattern = re.compile(r'(^[^\n;]*\b' + re.escape(symbol) + r'\([^;]*?\)\n\{)', re.M)
    matches = list(pattern.finditer(files[name]))
    assert len(matches) == 1, (name, symbol, len(matches))
    match = matches[0]
    files[name] = files[name][:match.end()] + '\n    ' + statement + files[name][match.end():]

def phase(name, symbol, result_type, actuals, number):
    pattern = re.compile(r'(^[^\n;]*\b' + re.escape(symbol) + r'\([^;]*?\))\n\{', re.M)
    matches = list(pattern.finditer(files[name]))
    assert len(matches) == 1, (name, symbol, len(matches))
    match = matches[0]
    signature = match.group(1)
    renamed = signature.replace(symbol + '(', 'c_z05_impl_' + symbol + '(')
    if not renamed.startswith('BUSTER_C_INTERNAL '):
        renamed = 'BUSTER_C_INTERNAL ' + renamed
    files[name] = files[name][:match.start()] + signature + ';\n' + renamed + '\n{' + files[name][match.end():]
    files[name] += ('\n' + signature + '\n{\n    u32 previous = c_z05_phase_enter(' + str(number) + ');\n    '
                    + result_type + ' result = c_z05_impl_' + symbol + '(' + actuals + ');\n'
                    + '    c_z05_phase_enter(previous);\n    return result;\n}\n')

helper = r'''
#include <stdio.h>
#include <stdlib.h>

BUSTER_C_DATA u32 c_z05_phase;
BUSTER_C_SHARED u32 c_z05_phase_enter(u32 phase)
{
    u32 previous = c_z05_phase;
    c_z05_phase = phase;
    return previous;
}

BUSTER_C_SHARED void c_z05_literal_probe(u32 kind, String8 spelling, u64 first, u64 second)
{
    static FILE* stream;
    static u64 serial;
    static u64 traffic;
    if (!stream)
    {
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        char const* path = getenv("BUSTER_Z05_TRACE");
        stream = path ? fopen(path, "wb") : 0;
        if (!stream)
        {
            fprintf(stderr, "Z05 trace requires a writable BUSTER_Z05_TRACE path\n");
            exit(2);
        }
    }
    if (spelling.length > UINT64_C(536870912) - traffic || serial == UINT64_C(10000000))
    {
        fprintf(stderr, "Z05 trace capacity exceeded; incomplete census\n");
        exit(2);
    }
    traffic += spelling.length;
    serial += 1;
    bool valid = fprintf(stream, "%llu %u %u %p %llu %llu %llu\n",
                         (unsigned long long)serial, kind, c_z05_phase, (void const*)spelling.pointer,
                         (unsigned long long)spelling.length, (unsigned long long)first, (unsigned long long)second) > 0;
    valid = valid && (!spelling.length || fwrite(spelling.pointer, 1, (size_t)spelling.length, stream) == spelling.length);
    valid = valid && fputc('\n', stream) != EOF && fflush(stream) == 0;
    if (!valid)
    {
        fprintf(stderr, "Z05 trace write failed; incomplete census\n");
        exit(2);
    }
}
'''
replace('c_internal.h', '// The matching-delimiter scan,', 'BUSTER_C_EXTERN u32 c_z05_phase_enter(u32 phase);\nBUSTER_C_EXTERN void c_z05_literal_probe(u32 kind, String8 spelling, u64 first, u64 second);\n\n// The matching-delimiter scan,')
replace('c_source.c', '#include "c_internal.h"', '#include "c_internal.h"\n' + helper)
replace('c_source.c', '    *value = parsed;\n    return any;', '    c_z05_literal_probe(1, spelling, base, index);\n    *value = parsed;\n    return any;')
at_entry('c_gen.c', 'c_ir_decode_quoted', 'c_z05_literal_probe(2, spelling, delimiter, 0);')
at_entry('c_gen.c', 'c_ir_count_quoted', 'c_z05_literal_probe(3, spelling, delimiter, 0);')
at_entry('c_gen.c', 'c_ir_decode_wide_quoted', 'c_z05_literal_probe(4, spelling, delimiter, width);')
at_entry('c_gen.c', 'c_ir_ext80_parse_integer', 'c_z05_literal_probe(5, spelling, 0, 0);')
at_entry('c_gen.c', 'c_ir_float_literal_value', 'c_z05_literal_probe(6, spelling, 0, 0);')
shape = '    bool result = c_ir_string_literal_range_shape(&preprocess, target, &start, &end, &decoded);'
assert files['c_gen.c'].count(shape) == 2
for kind in (7, 8):
    symbol = 'c_ir_' + ('decode' if kind == 7 else 'count') + '_string_literal_range_for_target'
    begin = files['c_gen.c'].index('BUSTER_C_SHARED bool ' + symbol)
    offset = files['c_gen.c'].index(shape, begin) + len(shape)
    record = ('\n    if (result)\n    {\n        c_z05_literal_probe(' + str(kind) + ', c_token_spelling(preprocess.spelling_base, preprocess.tokens[start]), end - start, decoded.element_width);\n    }')
    files['c_gen.c'] = files['c_gen.c'][:offset] + record + files['c_gen.c'][offset:]
phase('c_source.c', 'c_preprocess', 'CPreprocessResult', 'arena, source, options', 1)
phase('c_parse.c', 'c_parse_ast', 'CParserResult', 'arena, preprocess', 2)
phase('c_parse.c', 'c_analyze_semantics', 'CAnalysisResult', 'arena, preprocess, syntax', 3)
phase('c_gen.c', 'c_lower_to_ir_with_options', 'CIRLowerResult', 'arena, source_path, preprocess, parse, target, options', 4)
for name, text in files.items():
    (root / name).write_text(text)
