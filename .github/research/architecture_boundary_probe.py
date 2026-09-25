#!/usr/bin/env python3
"""Disposable hosted experiment; not a production compiler/build/test API.
Native compilation/configuration uses the existing build.c driver. No timing verdict.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

BASE = "ade6ac4b6ecb21f30b61b656439bac476c145e2f"
TREE = "4c5306221fdb22fccc929b55e333163742de17d0"
GEN = Path("src/buster/lib/compiler/frontend/c/c_gen.c")
E = Path(os.environ["ARCH_EVIDENCE"]).resolve()
E.mkdir(parents=True, exist_ok=True)
RECORDS = []
RESULTS = {}


def run(name, argv, cwd, timeout=600):
    out = E / (name + ".stdout")
    err = E / (name + ".stderr")
    record = {"name": name, "argv": [str(a) for a in argv], "cwd": str(cwd),
              "stdout": out.name, "stderr": err.name, "status": None}
    RECORDS.append(record)
    try:
        with out.open("wb") as stdout, err.open("wb") as stderr:
            proc = subprocess.run(record["argv"], cwd=cwd, stdout=stdout, stderr=stderr, timeout=timeout, check=False)
        record["status"] = proc.returncode
    except subprocess.TimeoutExpired:
        record["status"] = "timeout"
    finally:
        (E / "commands.json").write_text(json.dumps(RECORDS, indent=2) + "\n")
    print(name, "status=", record["status"], flush=True)
    return record["status"]


def required(name, argv, cwd, timeout=600):
    status = run(name, argv, cwd, timeout)
    if status != 0:
        raise RuntimeError(name + " failed: " + str(status))


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError("anchor count " + str(text.count(old)) + ": " + old[:100])
    return text.replace(old, new, 1)


def repair(original, structural):
    condition = "if (builtin == C_SYMBOL_BUILTIN_GENERIC || builtin == C_SYMBOL_BUILTIN_CHOOSE_EXPR)"
    local = "if (builtin == C_SYMBOL_BUILTIN_GENERIC || builtin == C_SYMBOL_BUILTIN_CHOOSE_EXPR ||\n                builtin == C_SYMBOL_BUILTIN_OBJECT_SIZE || builtin == C_SYMBOL_BUILTIN_CONSTANT_P)"
    text = once(original, condition, local if not structural else
                "if (c_ir_builtin_preparation_owner(builtin) != C_IR_BUILTIN_PREPARATION_CALLER)")
    old_diag = 'builder->failure_message = builtin == C_SYMBOL_BUILTIN_GENERIC ? S8("unterminated _Generic selection") :\n                                                                                     S8("unterminated __builtin_choose_expr selection");'
    new_diag = 'builder->failure_message = builtin == C_SYMBOL_BUILTIN_GENERIC ? S8("unterminated _Generic selection") :\n                                               builtin == C_SYMBOL_BUILTIN_CHOOSE_EXPR ? S8("unterminated __builtin_choose_expr selection") :\n                                                                                        S8("unterminated builtin operand");'
    text = once(text, old_diag, new_diag)
    if structural:
        anchor = "BUSTER_C_INTERNAL CSymbolBuiltin c_ir_token_builtin_kind(CIntegerIrBuilder* builder, CToken token);\n\n"
        helper = '''// Preparation authority, not builtin result semantics. Both eager prepasses
// must leave an owned operand to its existing selection/predicate continuation.
// No token annotations, new IR, retained cache, or mutable invalidation state.
typedef enum CIrBuiltinPreparationOwner
{
    C_IR_BUILTIN_PREPARATION_CALLER,
    C_IR_BUILTIN_PREPARATION_SELECTION,
    C_IR_BUILTIN_PREPARATION_UNEVALUATED,
} CIrBuiltinPreparationOwner;

BUSTER_C_INTERNAL CIrBuiltinPreparationOwner c_ir_builtin_preparation_owner(CSymbolBuiltin builtin)
{
    CIrBuiltinPreparationOwner result = C_IR_BUILTIN_PREPARATION_CALLER;
    switch (builtin)
    {
    case C_SYMBOL_BUILTIN_GENERIC:
    case C_SYMBOL_BUILTIN_CHOOSE_EXPR:
        result = C_IR_BUILTIN_PREPARATION_SELECTION;
        break;
    case C_SYMBOL_BUILTIN_OBJECT_SIZE:
    case C_SYMBOL_BUILTIN_CONSTANT_P:
        result = C_IR_BUILTIN_PREPARATION_UNEVALUATED;
        break;
    default:
        break;
    }
    return result;
}

'''
        text = once(text, anchor, anchor + helper)
        text = once(text, "CSymbolBuiltin builtin_kind = c_ir_token_builtin_kind(builder, token);\n        bool builtin_identity",
                    "CSymbolBuiltin builtin_kind = c_ir_token_builtin_kind(builder, token);\n        CIrBuiltinPreparationOwner preparation_owner = c_ir_builtin_preparation_owner(builtin_kind);\n        bool builtin_identity")
        text = once(text, ".deferred_calls = builtin_generic || builtin_choose_expr,",
                    ".deferred_calls = preparation_owner == C_IR_BUILTIN_PREPARATION_SELECTION,")
        text = once(text, "if (builtin_generic || builtin_choose_expr || builtin_object_size || builtin_constant_p)",
                    "if (preparation_owner != C_IR_BUILTIN_PREPARATION_CALLER)")
    return text


# Side effects are unsigned counter updates, never memory-error experiments.
# Each observation is sequenced after the full expression being tested.
NEW = [
    ("cp_assign", "__builtin_constant_p((hits = 1, 7))", "0", 0),
    ("cp_cond", "__builtin_constant_p((choose ? (hits = 1, 7) : (hits = 2, 9)))", "0", 0),
    ("cp_and", "__builtin_constant_p((choose && (hits = 1)))", "0", 0),
    ("cp_or", "__builtin_constant_p((choose || (hits = 1)))", None, 0),
    ("cp_nested", "__builtin_constant_p(((hits = 1, 7) + 2))", "0", 0),
    ("cp_call_group", "__builtin_constant_p((hits = mark(), 7))", "0", 0),
    ("bos0_assign", "__builtin_object_size((hits = 1, buffer), 0)", "~0UL", 0),
    ("bos1_assign", "__builtin_object_size((hits = 1, buffer), 1)", "~0UL", 0),
    ("bos2_assign", "__builtin_object_size((hits = 1, buffer), 2)", "0", 0),
    ("bos3_assign", "__builtin_object_size((hits = 1, buffer), 3)", "0", 0),
    ("bos_cond", "__builtin_object_size((choose ? (hits = 1, buffer) : (hits = 2, buffer)), 0)", "~0UL", 0),
    ("bos_call_group", "__builtin_object_size((hits = mark(), buffer), 0)", "~0UL", 0),
    ("chosen_cp", "__builtin_choose_expr(1, __builtin_constant_p((hits = 1, 7)), mark())", "0", 0),
    ("generic_cp", "_Generic(1, int: __builtin_constant_p((hits = 1, 7)), default: mark())", "0", 0),
]
CONTROLS = [
    ("cp_call", "__builtin_constant_p(mark())", "0", 0),
    ("bos_call", "__builtin_object_size((mark(), buffer), 0)", "~0UL", 0),
    ("choose_left", "__builtin_choose_expr(1, mark(), (hits = 2, 9))", "7", 1),
    ("choose_right", "__builtin_choose_expr(0, (hits = 2, 9), mark())", "7", 1),
    ("choose_discard", "__builtin_choose_expr(1, 7, mark())", "7", 0),
    ("generic_selected", "_Generic(1, int: mark(), default: (hits = 2, 9))", "7", 1),
    ("generic_control", "_Generic((hits = 1, 1), int: 7, default: mark())", "7", 0),
    ("sizeof_fixed", "sizeof(buffer[(choose ? hits++ : mark())])", "1", 0),
    ("sizeof_vla", "sizeof(vla[(hits++, 0)])", "3 * sizeof(int)", 1),
    ("ordinary_group", "(hits = 1, 7)", "7", 1),
    ("lazy_and", "(0 && (hits = 1))", "0", 0),
    ("lazy_or", "(1 || (hits = 1))", "1", 0),
    ("active_and", "(choose && (hits = 1))", "1", 1),
    ("comma_order", "(mark(), (hits = hits + 1, 7))", "7", 2),
    ("ordinary_sibling", "(mark() + __builtin_constant_p((hits = 9, 7)))", "7", 1),
]


def fixture(path, rows):
    lines = ['extern int printf(const char *, ...);', 'static volatile unsigned hits;',
             'static volatile int choose = 1;', 'static char buffer[32];',
             'static unsigned mark(void) { hits += 1; return 7; }',
             'static unsigned long identity(unsigned long v) { return v; }']
    names = []
    for label, expression, expected, expected_hits in rows:
        for context in ("init", "arg", "return", "discard"):
            name = label + "_" + context
            names.append(name)
            if context == "return":
                lines.append('static unsigned long payload_' + name + '(void) { int n = 3; int vla[2][n]; return (unsigned long)(' + expression + '); }')
            lines.append('static int case_' + name + '(void) { int n = 3; int vla[2][n]; hits = 0;')
            if context == "init":
                lines.append('unsigned long value = (unsigned long)(' + expression + ');')
            elif context == "arg":
                lines.append('unsigned long value = identity((unsigned long)(' + expression + '));')
            elif context == "return":
                lines.append('unsigned long value = payload_' + name + '();')
            else:
                lines.append('(void)(' + expression + '); unsigned long value = 0;')
            value_test = '0' if expected is None or context == "discard" else '(value != (unsigned long)(' + expected + '))'
            lines.append('unsigned observed = hits; int bad = (observed != ' + str(expected_hits) + ') || ' + value_test + ';')
            lines.append('printf("CASE ' + name + ' hits=%u value=%lu bad=%d\\n", observed, value, bad); return bad; }')
    lines.append('int main(void) { int failures = 0;')
    lines.extend('failures += case_' + name + '();' for name in names)
    lines.append('printf("TOTAL cases=' + str(len(names)) + ' failures=%d\\n", failures); return failures != 0; }')
    path.write_text('\n'.join(lines).replace('\\n', '\\n') + '\n')
    return len(names)


def configure_build(root, name):
    driver = root / '.architecture-driver'
    required(name + '-bootstrap', ['clang', '-Isrc', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-g', 'build.c', '-o', driver], root)
    required(name + '-generate', [driver, 'generate', '--cc', 'clang', '--config', 'Release', '--no-sanitize', '--no-fuzz', '--no-lto', '--linker', 'DEFAULT', '--', '-DBUSTER_DEBUG_INFO=OFF'], root)
    required(name + '-build', [driver, 'build', '--config', 'Release', '-t', 'ide'], root, 900)
    shutil.copyfile(root / 'build/CMakeCache.txt', E / (name + '-CMakeCache.txt'))
    binary = root / 'build/Release/ide'
    RESULTS[name + '_compiler_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
    return binary


def fixed_point(root, binary, name):
    # Deliberately do not call test_self_host: that target also runs benchmarks.
    # This is the documented two-generation compile/cmp subset, not the full gate.
    a, b = root / 'build/arch-stage1', root / 'build/arch-stage2'
    args = ['cc', '-Isrc', '-Ibuild/generated', '-DBUSTER_UNITY_BUILD=1', '-DBUSTER_INCLUDE_TESTS=0', '-g', '-v', 'src/buster/apps/ide/ide.c', '-lm']
    first = run(name + '-stage1', [binary, *args, '-o', a], root, 900)
    second = run(name + '-stage2', [a, *args, '-o', b], root, 900) if first == 0 else 'not-run'
    equal = first == 0 and second == 0 and hashlib.sha256(a.read_bytes()).digest() == hashlib.sha256(b.read_bytes()).digest()
    RESULTS[name + '_fixed_point'] = {'stage1': first, 'stage2': second, 'equal': equal,
        'sha256': hashlib.sha256(a.read_bytes()).hexdigest() if first == 0 else None,
        'normal_gate_benchmarks': 'not-run'}


def instrument(text):
    text = once(text, 'struct CIntegerIrBuilder\n{', 'struct CIntegerIrBuilder\n{\n    u64 research_control_visits;\n    u64 research_control_groups;')
    text = once(text, 'u32 index = frame->as.prepare_control.index++;', 'u32 index = frame->as.prepare_control.index++;\n        builder->research_control_visits += 1;')
    text = once(text, 'if (!parentheses && !brackets)', 'builder->research_control_groups += parentheses || brackets;\n        if (!parentheses && !brackets)')
    text = once(text, '        module->lowered_function_count += 1;', '        string_print(S8("ARCH_CONTROL name={S8} visits={u64} groups={u64}\\n"), declaration.name, builder.research_control_visits, builder.research_control_groups);\n        module->lowered_function_count += 1;')
    return text


def main():
    base = Path(sys.argv[1]).resolve()
    if git(base, 'rev-parse', 'HEAD') != BASE or git(base, 'rev-parse', 'HEAD^{tree}') != TREE:
        raise RuntimeError('source identity mismatch')
    if git(base, 'hash-object', str(GEN)) != 'ede2de412850975123da3f4a473f017591655a75':
        raise RuntimeError('frontend blob mismatch')
    original = (base / GEN).read_text()
    RESULTS['base'] = {'commit': BASE, 'tree': TREE, 'frontend_blob': git(base, 'hash-object', str(GEN))}
    RESULTS['cases'] = {'new': fixture(E / 'new.c', NEW), 'controls': fixture(E / 'controls.c', CONTROLS)}
    required('tools', ['bash', '-c', 'clang --version; gcc --version; cmake --version; ninja --version; uname -a'], base)
    for compiler in ('clang', 'gcc'):
        for opt in ('-O0', '-O2'):
            for group in ('new', 'controls'):
                name = 'reference-' + compiler + opt + '-' + group
                binary = E / name
                required(name + '-compile', [compiler, '-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', opt, E / (group + '.c'), '-o', binary], base)
                required(name + '-run', [binary], base)
    roots = {'baseline': base}
    baseline_binary = configure_build(base, 'baseline')
    fixed_point(base, baseline_binary, 'baseline')
    for variant in ('local', 'structural'):
        root = base.parent / ('arch-' + variant)
        required(variant + '-worktree', ['git', '-C', base, 'worktree', 'add', '--detach', root, BASE], base)
        (root / GEN).write_text(repair(original, variant == 'structural'))
        git(root, 'add', str(GEN))
        RESULTS[variant + '_tree'] = git(root, 'write-tree')
        (E / (variant + '.patch')).write_text(git(root, 'diff', '--cached') + '\n')
        roots[variant] = root
    for variant, root in roots.items():
        binary = baseline_binary if variant == 'baseline' else configure_build(root, variant)
        for group in ('new', 'controls'):
            for ssa in ('-ffrontend-ssa', '-fno-frontend-ssa'):
                for mode in ('none', 'mir-stack', 'fast', 'quality'):
                    name = variant + '-' + group + '-' + ssa.lstrip('-') + '-' + mode
                    output = E / name
                    status = run(name + '-compile', [binary, 'cc', '-target', 'x86_64-linux', '-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-g0', ssa, '-fregister-allocator=' + mode, '-fverify-codegen', E / (group + '.c'), '-o', output], root)
                    RESULTS[name] = {'compile': status, 'run': run(name + '-run', [output], root) if status == 0 else 'not-run'}
            run(variant + '-' + group + '-object', [binary, 'cc', '-std=gnu17', '-g0', '-c', E / (group + '.c'), '-o', E / (variant + '-' + group + '.o')], root)
            run(variant + '-' + group + '-llvm', [binary, 'cc', '-std=gnu17', '-S', '-emit-llvm', E / (group + '.c'), '-o', E / (variant + '-' + group + '.ll')], root)
        if variant in ('baseline', 'structural'):
            RESULTS[variant + '_suite'] = run(variant + '-suite', [binary, 'test', '--ci=1', '--verbose=1'], root, 900)
        if variant == 'structural':
            fixed_point(root, binary, variant)
            for target in ('x86_64-windows', 'x86_64-macos', 'aarch64-linux', 'aarch64-windows', 'aarch64-macos'):
                for ssa in ('-ffrontend-ssa', '-fno-frontend-ssa'):
                    name = variant + '-' + target + '-' + ssa.lstrip('-')
                    RESULTS[name] = run(name, [binary, 'cc', '-target', target, '-std=gnu17', '-g0', ssa, '-fverify-codegen', '-c', E / 'new.c', '-o', E / (name + '.o')], root)
        (E / 'results.json').write_text(json.dumps(RESULTS, indent=2) + '\n')
    # Count actual control-preparation visits on the same source, then prove
    # diagnostic instrumentation did not change that variant's emitted object.
    for variant in ('baseline', 'structural'):
        root = roots[variant]
        clean = (root / GEN).read_text()
        try:
            (root / GEN).write_text(instrument(clean))
            (E / (variant + '-counter.patch')).write_text(git(root, 'diff', 'HEAD') + '\n')
            required(variant + '-counter-build', [root / '.architecture-driver', 'build', '--config', 'Release', '-t', 'ide'], root, 900)
            binary = root / 'build/Release/ide'
            for group in ('new', 'controls'):
                obj = E / (variant + '-counter-' + group + '.o')
                name = variant + '-counter-' + group
                status = run(name, [binary, 'cc', '-std=gnu17', '-g0', '-c', E / (group + '.c'), '-o', obj], root)
                original_obj = E / (variant + '-' + group + '.o')
                RESULTS[name] = {'compile': status, 'object_equal': status == 0 and original_obj.exists() and original_obj.read_bytes() == obj.read_bytes()}
        finally:
            (root / GEN).write_text(clean)
    RESULTS['performance'] = 'No timings, RSS, PMU measurements, desktop tests, or 9700X launches. Counters are diagnostic only.'
    (E / 'results.json').write_text(json.dumps(RESULTS, indent=2) + '\n')
    bad = [k for k, v in RESULTS.items() if k.startswith(('local-new-', 'structural-new-', 'local-controls-', 'structural-controls-')) and isinstance(v, dict) and (v.get('compile') != 0 or v.get('run') != 0)]
    if bad:
        raise RuntimeError('candidate failures: ' + repr(bad))


try:
    main()
finally:
    (E / 'results.json').write_text(json.dumps(RESULTS, indent=2) + '\n')
    (E / 'commands.json').write_text(json.dumps(RECORDS, indent=2) + '\n')
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in E.iterdir() if p.is_file() and p.name != 'sha256.json'}
    (E / 'sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
