from pathlib import Path


def replace_once(path, before, after):
    p = Path(path)
    text = p.read_text()
    assert text.count(before) == 1, (path, before, text.count(before))
    p.write_text(text.replace(before, after, 1))


replace_once('src/buster/compiler/c_preprocessor.c', '    { "overloadable", 1 },\n', '')
replace_once('src/buster/compiler/c_preprocessor.c', '    { "clang::overloadable", 1 },\n', '')
replace_once('src/buster/compiler/c_preprocessor_feature_test.c', '"mode", "naked", "overloadable", "packed",', '"mode", "naked", "packed",')
replace_once('src/buster/compiler/c_preprocessor_feature_test.c', '"cleanup", "alias", "ifunc",', '"cleanup", "overloadable", "alias", "ifunc",')
replace_once('src/buster/compiler/c_preprocessor_feature_test.c', '{ "gnu::packed_typo", 0 },', '{ "gnu::packed_typo", 0 }, { "clang::overloadable", 0 }, { "gnu::overloadable", 0 },')
replace_once('docs/c-preprocessor.md', '  `overloadable`, `packed`, `section`, `target`, `transparent_union`, `used`,', '  `packed`, `section`, `target`, `transparent_union`, `used`,')
replace_once('docs/c-preprocessor.md', '(Clang-specific type/overload\n  attributes use `clang::`)', '(the Clang-specific vector\n  attribute uses `clang::`)')
replace_once('docs/c-preprocessor.md', 'Accepted but unimplemented attributes remain zero, including `cleanup`,\n', 'Accepted but unimplemented attributes remain zero, including `cleanup`,\n`overloadable` (native calls and mangled-symbol emission are explicitly refused),\n')
