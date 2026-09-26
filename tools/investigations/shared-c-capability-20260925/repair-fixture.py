#!/usr/bin/env python3
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
path = root / 'src/buster/tests/compiler/frontend/c/c_test.c'
text = path.read_text()
old = 'BUSTER_INTERNAL UnitTestResult c_test_comma_condition_evaluation'
if text.count(old) != 1:
    raise RuntimeError('unexpected test source')
text = text.replace(old, 'BUSTER_GLOBAL_LOCAL UnitTestResult c_test_comma_condition_evaluation', 1)
fixture = text.index('BUSTER_GLOBAL_LOCAL UnitTestResult c_test_comma_condition_evaluation')
start = text.index('    String8 source_text = S8(\n', fixture)
end = text.index('    );\n', start) + len('    );\n')
lines = text[start:end].splitlines()[1:-1]
source = ''.join(json.loads(line.strip()) for line in lines)
if hashlib.sha256(source.encode()).hexdigest() != '06345533b6d9a4e846838038941b7d1ea10d950a24ee1aafd91d5056c6fcc7b5':
    raise RuntimeError('fixture content drifted')
replacement = '    String8 source_parts[] = {\n' + ''.join('        S8(' + line.strip() + '),\n' for line in lines) + '    };\n    String8 source_text = string_join_arena(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(source_parts), false);\n'
text = text[:start] + replacement + text[end:]
path.write_text(text)
