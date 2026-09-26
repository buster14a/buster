#!/usr/bin/env python3
"""Second, independent mechanism: C falloff without result use retains a return edge."""
import ast
import hashlib
import os
from pathlib import Path

if os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('CI_ENABLED') != 'true':
    raise SystemExit('Hosted correctness only')
root = Path(os.environ['GITHUB_WORKSPACE'])
source_path = root / 'tools/investigations/false-necessity-20260925-astra/probe.py'
source = source_path.read_text()
EXPECTED_HARNESS = '1c5fd8aaba89c781662cdb869f86bd0722fec656d4f72917320445f5c85c5519'
assert hashlib.sha256(source.encode()).hexdigest() == EXPECTED_HARNESS
old = '''            // Only the root body task has no continuation, so this is the end
            // of a non-void function's body. Reaching the closing brace is
            // undefined only if the caller uses the value (C 6.9.1p12), so
            // this is not a refusal: terminate with unreachable, which is what
            // Clang and GCC emit. sbase's dc ends `regname` with a call to its
            // own non-noreturn error(), and the bc its tests drive is
            // yacc-generated with the same shape.
            IrSourceRange unreachable_source = declaration_source;
            IrInstruction unreachable = c_ir_instruction_initialize(IR_OPCODE_UNREACHABLE, builder->void_type);
            c_ir_append_instruction(builder, unreachable, unreachable_source);
            block->terminated = true;'''
new = '''            // Experiment: an unused missing result does not erase the return
            // edge in C. Select a deterministic arbitrary value through the
            // existing typed constructor, retaining ordinary validation.
            CToken token = builder->preprocess.tokens[builder->parse.declarations[builder->declaration_index].token_start];
            IrValueId value = c_ir_emit_zero_value(builder, builder->return_type, token);
            if (value.value == IR_ID_UNDERLYING_INVALID ||
                !c_ir_terminate(builder, IR_OPCODE_RETURN, &value, 1, 0, 0, declaration_source))
            {
                return false;
            }'''
base = 'static volatile unsigned seen;\n'
fall = base + 'static int effect(void) { seen = 41u; }\n'
cases = {
    'direct': fall + 'int main(void) { effect(); return seen != 41u; }\n',
    'renamed': base + 'static int effect(void) { seen=41u; return 7; }\nint main(void) { effect(); return seen != 41u; }\n',
    'void_control': base + 'static void effect(void) { seen=41u; }\nint main(void) { effect(); return seen != 41u; }\n',
    'indirect': fall + 'int main(void) { int (*p)(void)=effect; p(); return seen != 41u; }\n',
    'shadow': fall + 'int main(void) { int (*p)(void)=effect; { int (*effect)(void)=p; effect(); } return seen != 41u; }\n',
    'conditional': base + 'static int effect(int x) { seen += 1u; if (x) return 7; }\nint main(void) { int x=effect(1); effect(0); return x != 7 || seen != 2u; }\n',
    'lifetime': base + 'static int effect(unsigned n) { unsigned a[n]; a[n-1]=41u; seen=a[n-1]; }\nint main(void) { effect(3); effect(5); return seen != 41u; }\n',
    'value': base + 'static int effect(int x) { seen += 1u; if (x) return 7; }\nint main(void) { return effect(1) != 7 || seen != 1u; }\n',
    'cast_void': fall + 'int main(void) { (void)effect(); return seen != 41u; }\n',
    'comma': fall + 'int main(void) { int x=(effect(), 5); return x != 5 || seen != 41u; }\n',
    'double': fall.replace('static int effect', 'static double effect') + 'int main(void) { effect(); return seen != 41u; }\n',
    'long_double': fall.replace('static int effect', 'static long double effect') + 'int main(void) { effect(); return seen != 41u; }\n',
    'pointer': fall.replace('static int effect', 'static int *effect') + 'int main(void) { effect(); return seen != 41u; }\n',
    'i128': fall.replace('static int effect', 'static __int128 effect') + 'int main(void) { effect(); return seen != 41u; }\n',
    'struct': 'struct Pair { int a, b; };\n' + fall.replace('static int effect', 'static struct Pair effect') + 'int main(void) { effect(); return seen != 41u; }\n',
    'large_struct': 'struct Big { unsigned a[1024]; };\n' + fall.replace('static int effect', 'static struct Big effect') + 'int main(void) { effect(); return seen != 41u; }\n',
    'cleanup': base + 'static void clean(unsigned *x) { seen += *x; }\nstatic int effect(void) { unsigned x __attribute__((cleanup(clean)))=41u; }\nint main(void) { effect(); return seen != 41u; }\n',
    'main_zero': 'int main(void) {}\n',
    'attribute': '_Noreturn void _Exit(int);\n__attribute__((noreturn)) void stop(int x) { _Exit(x); }\nint main(void) { stop(0); }\n',
    'real_abort': 'void abort(void);\nint main(void) { abort(); return 0; }\n',
}
invalid = {
    'arity': 'static int effect(int x) { return x; }\nint main(void) { effect(); return 0; }\n',
    'conflicting': 'static void effect(int);\nstatic int effect(int x) { return x; }\nint main(void) { return 0; }\n',
    'syntax': 'static int effect(void) { return }\nint main(void) { effect(); return 0; }\n',
    'bare_return': 'static int effect(void) { return; }\nint main(void) { effect(); return 0; }\n',
}
replacements = {'OLD': old, 'NEW': new, 'BASE': base, 'CASES': cases, 'INVALID': invalid}
lines = source.splitlines(keepends=True)
changes = []
for node in ast.parse(source).body:
    if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
        name = node.targets[0].id
        if name in replacements:
            changes.append((node.lineno-1, node.end_lineno, name + ' = ' + repr(replacements[name]) + '\n'))
assert len(changes) == len(replacements)
for first, last, text in sorted(changes, reverse=True):
    lines[first:last] = [text]
source = ''.join(lines).replace('false-necessity-', 'return-edge-')
# Add a genuine separate-TU ABI/identity control and a non-oracle used-falloff input.
marker = "    selfhost(label, work, ide)\n"
extra = '''    separate = out / label / 'separate'
    separate.mkdir(parents=True, exist_ok=True)
    callee = separate / 'callee.c'
    caller = separate / 'caller.c'
    callee.write_text('extern volatile unsigned seen; int effect(void) { seen=41u; }\\n')
    caller.write_text('volatile unsigned seen; int effect(void); int main(void) { effect(); return seen!=41u; }\\n')
    for form in ['ssa', 'memory']:
        common = [ide, 'cc', '-target', 'x86_64-linux'] + FLAGS + ['-fverify-codegen', '-fregister-allocator=fast', '-fno-machine-fallback', '-ffrontend-ssa' if form == 'ssa' else '-fno-frontend-ssa']
        obj = separate / (form + '.o')
        status = run(label + '/separate/' + form + '/compile', common + ['-c', callee, '-o', obj], work)
        if status == 0:
            executable = separate / (form + '.exe')
            linked = run(label + '/separate/' + form + '/link', ['clang', '-O0', caller, obj, '-o', executable], work)
            if linked == 0:
                run(label + '/separate/' + form + '/execute', [executable], work, 10)
    # This source has undefined behavior if executed; compile-only and no value oracle.
    no_oracle = separate / 'used-falloff-NOT-A-VALID-RUNTIME-ORACLE.c'
    no_oracle.write_text('int effect(void) {} int main(void) { return effect(); }\\n')
    run(label + '/used-falloff-compile-only', [ide, 'cc'] + FLAGS + ['-c', no_oracle, '-o', separate / 'used.o'], work)
    selfhost(label, work, ide)
'''
assert source.count(marker) == 1
source = source.replace(marker, extra)
out = root / 'evidence'
out.mkdir(exist_ok=True)
(out / 'expanded-probe.py').write_text(source)
# Executed solely on the explicitly enabled hosted runner, never a local workstation.
exec(compile(source, str(out / 'expanded-probe.py'), 'exec'), {'__name__': '__main__'})
