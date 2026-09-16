from pathlib import Path
p=Path('src/buster/lib/compiler/frontend/c/c_parse.c')
s=p.read_text()
a=s.index('<<<<<<< HEAD\n'); b=s.index('=======\n',a); c=s.index('\n>>>>>>> ',b); e=s.index('\n',c+1)
s=s[:a]+s[b+8:c+1]+s[e+1:]
s=s.replace('bool seen_unsigned, bool seen_float16, bool seen_float, bool seen_double,','bool seen_unsigned, bool seen_float16, bool seen_bfloat16, bool seen_float, bool seen_double,')
s=s.replace('(u32)seen_float16 + (u32)seen_float + (u32)seen_double + (u32)seen_int128;', '(u32)seen_float16 + (u32)seen_bfloat16 + (u32)seen_float + (u32)seen_double + (u32)seen_int128;')
s=s.replace('    else if (seen_float16)\n    {\n        valid &=', '    else if (seen_float16 || seen_bfloat16)\n    {\n        valid &= !seen_bfloat16 || !seen_complex;\n        valid &=', 1)
s=s.replace('    u32 bfloat16_count = 0;\n','')
s=s.replace('            seen_bfloat16 = true;\n            bfloat16_count += 1;', '            duplicate |= seen_bfloat16;\n            seen_bfloat16 = true;')
s=s.replace('seen_int, seen_signed, seen_unsigned, seen_float16, seen_float, seen_double, seen_int128, seen_complex, seen_imaginary,', 'seen_int, seen_signed, seen_unsigned, seen_float16, seen_bfloat16, seen_float, seen_double, seen_int128, seen_complex, seen_imaginary,')
needle='''        else if (seen_float16)
        {
            type.kind = C_TYPE_FLOAT16;
        }'''
assert s.count(needle)==1
s=s.replace(needle,'''        else if (seen_bfloat16)
        {
            type.kind = C_TYPE_BFLOAT16;
        }
'''+needle)
needle='''        result = c_parse_float16_specifier_valid(seen_void, seen_bool, seen_char, seen_short, seen_int, seen_signed, seen_unsigned, seen_int128, seen_float,
                                                 seen_double, seen_va_list, long_count) &&
                         !seen_float16 && !seen_complex && bfloat16_count == 1
                     ? C_TYPE_BFLOAT16
                     : C_TYPE_INVALID;'''
assert s.count(needle)==1
s=s.replace(needle,'        result = C_TYPE_BFLOAT16;')
assert '<<<<<<<' not in s and 'bfloat16_count' not in s
p.write_text(s)
p=Path('src/buster/tests/compiler/frontend/c/c_test.c')
s=p.read_text()
old='S8("_Float16 _Complex _Float16"), S8("const _Float16 const unsigned")};'
new='S8("_Float16 _Complex _Float16"), S8("const _Float16 const unsigned"),\n        S8("long __bf16"), S8("__bf16 long"), S8("unsigned __bf16"), S8("__bf16 unsigned"),\n        S8("__bf16 __bf16"), S8("__bf16 _Float16"), S8("_Float16 __bf16"), S8("__bf16 _Complex"),\n        S8("_Complex __bf16"), S8("__bf16 _Imaginary"), S8("__bf16 _Bool"), S8("void __bf16"),\n        S8("__bf16 int"), S8("float __bf16"), S8("__bf16 double"), S8("const __bf16 const unsigned")};'
assert s.count(old)==1
p.write_text(s.replace(old,new))
