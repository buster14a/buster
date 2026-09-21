from pathlib import Path
p = Path('src/buster/tests/compiler/frontend/c/c_test.c')
s = p.read_text()
start = s.index('BUSTER_GLOBAL_LOCAL String8 const c_test_enum_bit_field_source = S8(')
end = s.index('\n);', start) + len('\n);')
old = s[start:end]
body = old[old.index('\n') + 1:-len('\n);')]
a, b = body.split('    "int main(void)\\n"', 1)
b = '    "int main(void)\\n"' + b
new = ('BUSTER_GLOBAL_LOCAL String8 c_test_enum_bit_field_source(Arena* arena)\n{\n'
       '    String8 parts[] = {S8(\n' + a.rstrip() + '\n    ), S8(\n' + b + '\n    )};\n'
       '    return string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), false);\n}')
s = s[:start] + new + s[end:]
s = s.replace('c_preprocess(temporary.arena, c_test_enum_bit_field_source,',
              'c_preprocess(temporary.arena, c_test_enum_bit_field_source(temporary.arena),')
s = s.replace('    if (BUSTER_REQUIRE(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(c_test_enum_bit_field_source))))',
              '    String8 source_text = c_test_enum_bit_field_source(arguments->arena);\n'
              '    if (BUSTER_REQUIRE(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(source_text))))')
s = s.replace('c_test_enum_wide_successor_source, c_test_enum_bit_field_source};',
              'c_test_enum_wide_successor_source, c_test_enum_bit_field_source(arguments->arena)};')
p.write_text(s)
