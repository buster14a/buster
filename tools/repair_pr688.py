import os
from pathlib import Path
import re
import subprocess

HEAD = 'cf0107cc74b6e04304cb8a3dc2a065b2c3a8030c'
BASE = 'e68e4f62eb67ec8e432fe46277bfe6f9e0e472cf'
MAIN = '2142bf4b520cd82ca2dd3aeba5941cfd16ac48fd'
PARSE = 'src/buster/lib/compiler/frontend/c/c_parse.c'
DRIVER = 'src/buster/tests/compiler/driver/driver_test.c'
FRONTEND = 'src/buster/tests/compiler/frontend/c/c_test.c'
DOC = 'docs/agents/frontend/foundations.md'


def replace(text, old, new, count=1):
    actual = text.count(old)
    assert actual == count, (actual, count, old)
    return text.replace(old, new)


def repair_parse(text):
    pattern = re.compile(r'^<<<<<<< ' + HEAD + r'\n(.*?)^=======\n(.*?)^>>>>>>> ' + MAIN + r'\n', re.M | re.S)
    conflicts = list(pattern.finditer(text))
    assert len(conflicts) == 2
    assert 'c_parse_primitive_specifiers_valid' in conflicts[0][1]
    assert 'c_parse_float16_specifier_valid' in conflicts[0][2]
    assert conflicts[1][1].lstrip().startswith('bool valid_specifiers =')
    for match in reversed(conflicts):
        value = match[2] + '}\n\n' + match[1] if match == conflicts[0] else match[1]
        text = text[:match.start()] + value + text[match.end():]
    text = replace(text,
        'bool seen_short, bool seen_int, bool seen_signed, bool seen_unsigned, bool seen_float, bool seen_double,\n',
        'bool seen_short, bool seen_int, bool seen_signed, bool seen_unsigned, bool seen_float16, bool seen_float, bool seen_double,\n')
    text = replace(text,
        '(u32)seen_float + (u32)seen_double + (u32)seen_int128;',
        '(u32)seen_float16 + (u32)seen_float + (u32)seen_double + (u32)seen_int128;')
    text = replace(text,
        '    else if (seen_float)\n    {\n        valid &=',
        '    else if (seen_float16)\n    {\n'
        '        valid &= c_parse_float16_specifier_valid(seen_void, seen_bool, seen_char, seen_short, seen_int, seen_signed,\n'
        '            seen_unsigned, seen_int128, seen_float, seen_double, seen_va_list, long_count);\n'
        '    }\n'
        '    else if (seen_float)\n    {\n        valid &=')
    text = replace(text, 'c_parse_complex_kind(seen_float, seen_double,',
                         'c_parse_complex_kind(seen_float16, seen_float, seen_double,', 2)
    text = replace(text,
        'seen_int, seen_signed, seen_unsigned, seen_float, seen_double, seen_int128, seen_complex, seen_imaginary,\n',
        'seen_int, seen_signed, seen_unsigned, seen_float16, seen_float, seen_double, seen_int128, seen_complex, seen_imaginary,\n', 2)
    text = replace(text,
        'else if (string_equal(spelling, S8("_Float16")))\n        {\n            seen_float16 = true;',
        'else if (string_equal(spelling, S8("_Float16")))\n        {\n            duplicate |= seen_float16;\n            seen_float16 = true;', 2)
    text = replace(text,
        '        else if (seen_float)\n        {\n            type.kind = C_TYPE_FLOAT;\n        }',
        '        else if (seen_float16)\n        {\n            type.kind = C_TYPE_FLOAT16;\n        }\n'
        '        else if (seen_float)\n        {\n            type.kind = C_TYPE_FLOAT;\n        }')
    text = replace(text,
        'bool invalid = seen_void || seen_bool || seen_char || seen_short || seen_int || seen_signed || seen_unsigned || seen_float ||\n',
        'bool invalid = seen_void || seen_bool || seen_char || seen_short || seen_int || seen_signed || seen_unsigned || seen_float || seen_float16 ||\n')
    old = '''    // `_Float16` shares a specifier set with nothing, so its arm precedes
    // every kind it refuses to combine with: `_Float16 char` is not a `char`.
    // c_parse_primitive_type asks the same question ahead of its own ladder.
    else if (seen_float16)
    {
        result = c_parse_float16_specifier_valid(seen_void, seen_bool, seen_char, seen_short, seen_int, seen_signed, seen_unsigned, seen_int128, seen_float,
                                                 seen_double, seen_va_list, long_count)
                     ? C_TYPE_FLOAT16
                     : C_TYPE_INVALID;
    }'''
    text = replace(text, old, '''    else if (seen_float16)
    {
        result = C_TYPE_FLOAT16;
    }''')
    assert not re.search(r'^(<<<<<<<|=======|>>>>>>>)', text, re.M)
    return text


def repair_driver(text):
    text = replace(text,
        'S8("_Imaginary double"), S8("_Complex int"), S8("_Complex char")};',
        'S8("_Imaginary double"), S8("_Complex int"), S8("_Complex char"),\n'
        '        S8("long _Float16"), S8("_Float16 long"), S8("unsigned _Float16"), S8("_Float16 int"),\n'
        '        S8("_Float16 _Float16"), S8("_Float16 float"), S8("_Float16 double"), S8("void _Float16"),\n'
        '        S8("_Complex long _Float16"), S8("_Float16 _Complex int"), S8("const _Float16 const unsigned")};')
    text = replace(text,
        'String8 control_specifiers[] = {S8("long long int"), S8("double _Complex")};',
        'String8 control_specifiers[] = {S8("long long int"), S8("double _Complex"), S8("_Float16"), S8("_Float16 _Complex")};')
    text = replace(text,
        '''                    u64 expected_size = control == 0 ? target_data_layout(parsed_target.target).long_long_integer.size
                                                     : 2u * target_data_layout(parsed_target.target).double_type.size;''',
        '''                    TargetDataLayout layout = target_data_layout(parsed_target.target);
                    u64 control_sizes[] = {layout.long_long_integer.size, 2u * layout.double_type.size,
                        layout.float16_type.size, 2u * layout.float16_type.size};
                    u64 expected_size = control_sizes[control];''')
    text = replace(text,
        '''                                bool macho_names = parsed_target.target.os == OPERATING_SYSTEM_MACOS ||
                                                   parsed_target.target.os == OPERATING_SYSTEM_IOS;
''', '')
    text = replace(text,
        '''                                String8 serialized_v_name = macho_names ? S8("_v") : S8("v");
                                String8 serialized_take_name = macho_names ? S8("_take") : S8("take");
                                ObjectSymbol* serialized_v = compiler_driver_test_symbol_by_name(&round_trip, serialized_v_name);''',
        '''                                // object_read removes Mach-O's leading underscore; use canonical names
                                // here while retaining each format's defined-symbol and size contracts.
                                ObjectSymbol* serialized_v = compiler_driver_test_symbol_by_name(&round_trip, S8("v"));''')
    text = replace(text,
        'ObjectSymbol* serialized_take = compiler_driver_test_symbol_by_name(&round_trip, serialized_take_name);',
        'ObjectSymbol* serialized_take = compiler_driver_test_symbol_by_name(&round_trip, S8("take"));')
    text = replace(text,
        'S8("long long double"), S8("float double"), S8("_Imaginary double")};',
        'S8("long long double"), S8("float double"), S8("_Imaginary double"), S8("long _Float16"),\n'
        '                S8("_Float16 long"), S8("unsigned _Float16"), S8("_Float16 int"), S8("_Float16 _Float16")};')
    text = replace(text,
        'String8 oracle_valid[] = {S8("long long int"), S8("double _Complex")};',
        'String8 oracle_valid[] = {S8("long long int"), S8("double _Complex"), S8("_Float16"), S8("_Float16 _Complex")};')
    return text


def repair_frontend(text):
    text = replace(text,
        'S8("long long long"), S8("int int"), S8("float float")};',
        'S8("long long long"), S8("int int"), S8("float float"),\n'
        '        S8("long _Float16"), S8("_Float16 long"), S8("unsigned _Float16"), S8("_Float16 unsigned"),\n'
        '        S8("_Float16 int"), S8("int _Float16"), S8("_Float16 _Float16"), S8("_Float16 float"),\n'
        '        S8("_Float16 double"), S8("void _Float16"), S8("_Bool _Float16"), S8("_Float16 char"),\n'
        '        S8("_Complex long _Float16"), S8("_Float16 _Complex int"), S8("_Imaginary _Float16"),\n'
        '        S8("_Float16 _Complex _Float16"), S8("const _Float16 const unsigned")};')
    text = replace(text,
        'S8("long _Complex double"), S8("_Complex"), S8("__complex__ double")};',
        'S8("long _Complex double"), S8("_Complex"), S8("__complex__ double"),\n'
        '        S8("_Float16"), S8("_Float16 _Complex"), S8("_Complex _Float16"), S8("__complex__ _Float16"), S8("const _Float16 const")};')
    text = replace(text,
        'C_TYPE_LONG_DOUBLE_COMPLEX, C_TYPE_DOUBLE_COMPLEX, C_TYPE_DOUBLE_COMPLEX};',
        'C_TYPE_LONG_DOUBLE_COMPLEX, C_TYPE_DOUBLE_COMPLEX, C_TYPE_DOUBLE_COMPLEX,\n'
        '        C_TYPE_FLOAT16, C_TYPE_FLOAT16_COMPLEX, C_TYPE_FLOAT16_COMPLEX, C_TYPE_FLOAT16_COMPLEX, C_TYPE_FLOAT16};')
    text = replace(text,
        '        2 * control_layout.double_type.size, 2 * control_layout.double_type.size,\n    };',
        '        2 * control_layout.double_type.size, 2 * control_layout.double_type.size,\n'
        '        control_layout.float16_type.size, 2 * control_layout.float16_type.size, 2 * control_layout.float16_type.size,\n'
        '        2 * control_layout.float16_type.size, control_layout.float16_type.size,\n    };')
    return text


def repair_doc(text):
    return replace(text,
        '''  and its `__complex`/`__complex__` aliases on the three real floating kinds
  remain valid. A type name refused this way pins `sizeof`/`_Alignof` to the''',
        '''  and its `__complex`/`__complex__` aliases on the three C99 real floating kinds
  remain valid. `_Float16` and its complex extension use the same validity
  gate in both scanners, retaining the binary16 specifier contract while
  rejecting contradictory or repeated half-type words with a diagnostic.
  A type name refused this way pins `sizeof`/`_Alignof` to the''')


def main():
    merge = subprocess.run(['git', 'merge-tree', '--write-tree', '--merge-base=' + BASE, HEAD, MAIN], text=True, capture_output=True)
    assert merge.returncode == 1, merge.stdout + merge.stderr
    conflicts = set(re.findall(r'^\d{6} [0-9a-f]{40} [123]\t(.+)$', merge.stdout, re.M))
    assert conflicts == {PARSE}, conflicts
    tree = merge.stdout.splitlines()[0]
    assert tree == '49043fda7707fb48525b4f35878d3c4ae20a31db', tree
    work = Path(os.environ['RUNNER_TEMP']) / 'pr688-source'
    work.mkdir()
    with (work / 'source.tar').open('wb') as f:
        subprocess.run(['git', 'archive', tree], stdout=f, check=True)
    subprocess.run(['tar', '-xf', str(work / 'source.tar'), '-C', str(work)], check=True)
    (work / 'source.tar').unlink()
    evidence = Path(os.environ['RUNNER_TEMP']) / 'pr688-evidence'
    evidence.mkdir()
    for path, repair in [(PARSE, repair_parse), (DRIVER, repair_driver), (FRONTEND, repair_frontend), (DOC, repair_doc)]:
        target = work / path
        target.write_text(repair(target.read_text()))
    subprocess.run(['git', 'read-tree', tree], check=True)
    for path in [PARSE, DRIVER, FRONTEND, DOC]:
        sha = subprocess.check_output(['git', 'hash-object', '-w', str(work / path)], text=True).strip()
        subprocess.run(['git', 'update-index', '--cacheinfo', '100644,' + sha + ',' + path], check=True)
    repaired_tree = subprocess.check_output(['git', 'write-tree'], text=True).strip()
    subprocess.run(['git', 'diff', '--check', MAIN, repaired_tree], check=True)
    delta = subprocess.check_output(['git', 'diff', '--no-color', MAIN, repaired_tree], text=True)
    (evidence / 'candidate-main.patch').write_text(delta)
    subprocess.run(['git', 'diff', '--stat', MAIN, repaired_tree], check=True)
    paths = subprocess.check_output(['git', 'diff', '--name-only', MAIN, repaired_tree], text=True).splitlines()
    expected_paths = subprocess.check_output(['git', 'diff', '--name-only', BASE, HEAD], text=True).splitlines()
    assert paths == expected_paths, (paths, expected_paths)
    (evidence / 'tree.txt').write_text(repaired_tree + '\n')
    for path in [PARSE, DRIVER, FRONTEND, DOC]:
        dst = evidence / 'source' / path
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes((work / path).read_bytes())
    for p in subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', HEAD], text=True).splitlines():
        if p.endswith('/object.c'):
            content = subprocess.check_output(['git', 'show', HEAD + ':' + p], text=True)
            lines = content.splitlines()
            for i, line in enumerate(lines):
                if "pointer[0] == '_'" in line:
                    print('NORMALIZATION', p, i + 1, '\n'.join(lines[max(0, i-6):i+12]))
    message = 'Merge main into PR 688 and repair normalized symbol/type-specifier regressions\n\nPreserve landed binary16 support in both specifier scanners, diagnose invalid and duplicate half-type sets, and extend registered acceptance controls. Match canonical Mach-O symbol names after object_read without weakening definition, kind, or format-specific size assertions.\n'
    candidate = subprocess.check_output(['git', 'commit-tree', repaired_tree, '-p', HEAD, '-p', MAIN], input=message, text=True).strip()
    branch = 'codex/repair-688-candidate-20260916'
    subprocess.run(['git', 'push', 'origin', candidate + ':refs/heads/' + branch], check=True)
    (evidence / 'candidate.txt').write_text(candidate + '\n')
    print('CANDIDATE', candidate, 'TREE', repaired_tree, 'BRANCH', branch)
    validate = Path(os.environ['RUNNER_TEMP']) / 'pr688-validate'
    subprocess.run(['git', 'worktree', 'add', '--detach', str(validate), candidate], check=True)
    with Path(os.environ['GITHUB_OUTPUT']).open('a') as f:
        f.write('candidate=' + candidate + '\n')
        f.write('source=' + str(validate) + '\n')
        f.write('evidence=' + str(evidence) + '\n')


if __name__ == '__main__':
    main()
