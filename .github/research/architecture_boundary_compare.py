#!/usr/bin/env python3
"""Bounded hosted follow-up: fair complete-local comparison, not a new repair root."""
import ast
import os
from pathlib import Path
replay = Path(__file__).with_name('architecture_boundary_replay.py')
namespace = {'__file__': str(replay), '__name__': 'components'}
exec(compile(replay.read_text().split('\nast.parse(source)')[0], str(replay), 'exec'), namespace)
components = namespace['source'].split('\ntry:\n    main()')[0]
body = Path(__file__).read_text().split('\n# EXECUTABLE COMPARISON BODY\n', 1)[1]
effective = components + '\n' + body
ast.parse(effective)
path = Path(os.environ['ARCH_EVIDENCE']) / 'comparison-experiment.py'
path.write_text(effective)
exec(compile(effective, str(path), 'exec'), {'__name__': '__main__'})

# EXECUTABLE COMPARISON BODY

def complete_local(original):
    text = repair(original, False)
    text = once(text,
        '(!indirect && c_ir_prepared_control_expression_contains(builder, index)) || c_ir_prepared_call_find(builder, callee_start))',
        '(!indirect && c_ir_prepared_control_expression_contains(builder, index)))')
    anchor = '        if (c_ir_lazy_operand_scan_deferred(lazy) || comma_sequence_depth != UINT32_MAX)'
    block = '''        CIrPreparedCall* existing_call = c_ir_prepared_call_find(builder, callee_start);
        if (existing_call)
        {
            // Complete local competitor: retains the independent ownership lists.
            if (!indirect && (builtin_generic || builtin_choose_expr || builtin_object_size || builtin_constant_p) &&
                existing_call->open_index == index + 1 && existing_call->close_index < end)
            {
                index = existing_call->close_index;
            }
            continue;
        }
'''
    return once(text, anchor, block + anchor)


def comparison_main():
    base = Path(sys.argv[1]).resolve()
    if git(base, 'rev-parse', 'HEAD') != BASE or git(base, 'rev-parse', 'HEAD^{tree}') != TREE:
        raise RuntimeError('base identity mismatch')
    if git(base, 'hash-object', str(GEN)) != 'ede2de412850975123da3f4a473f017591655a75':
        raise RuntimeError('frontend identity mismatch')
    original = (base / GEN).read_text()
    RESULTS['base'] = {'commit':BASE, 'tree':TREE}
    RESULTS['local_variant'] = 'Complete local repair: both control exclusions AND recorded-call replay, but independent literal policies.'
    RESULTS['structural_variant'] = 'The replay-aware shared owner from run 36186095616; no new production behavior.'
    RESULTS['prior'] = {'first_comparison':36183459094, 'replay_comparison':36186095616, 'handoff_comment':5838907405}
    RESULTS['cases'] = {'new':fixture(E / 'new.c', NEW), 'controls':fixture(E / 'controls.c', CONTROLS)}
    handed = [
        ('handoff_logic', '__builtin_constant_p((mark() && 0) || 1)', None, 0),
        ('handoff_arithmetic', '__builtin_constant_p((mark() && 0) + 9)', None, 0),
        ('handoff_cast', '__builtin_constant_p((unsigned char)(mark() || 1))', None, 0),
    ]
    RESULTS['cases']['handoff'] = fixture(E / 'handoff.c', handed)
    handoff = (E / 'handoff.c').read_text()
    handoff = once(handoff, 'static volatile int choose = 1;', 'static volatile int choose = 0;')
    handoff = once(handoff, 'static unsigned mark(void) { hits += 1; return 7; }',
                   'static unsigned mark(void) { hits += 1; return (unsigned)choose; }')
    (E / 'handoff.c').write_text(handoff)
    RESULTS['handoff_scope'] = 'Derived from the handed-off expressions with a volatile runtime return. Only no-evaluation is asserted. Predicate result bits are retained separately; #1224/#1225 value folding is unchanged and not claimed fixed.'
    required('tools', ['bash','-c','clang --version; gcc --version; cmake --version; ninja --version; uname -a'], base)
    for compiler in ('clang','gcc'):
        for opt in ('-O0','-O2'):
            for group in ('new','controls','handoff'):
                name = 'reference-' + compiler + opt + '-' + group
                binary = E / name
                required(name+'-compile', [compiler,'-std=gnu17','-fwrapv','-fno-strict-aliasing','-funsigned-char',opt,E/(group+'.c'),'-o',binary], base)
                required(name+'-run', [binary], base)
    structural_root = base.parent / 'arch-structural'
    required('structural-worktree', ['git','-C',base,'worktree','add','--detach',structural_root,BASE], base)
    for variant, root in (('local',base),('structural',structural_root)):
        (root / GEN).write_text(complete_local(original) if variant == 'local' else repair(original, True))
        install_canonical(root)
        git(root,'add',str(GEN))
        RESULTS[variant+'_tree'] = git(root,'write-tree')
        RESULTS[variant+'_frontend_blob'] = git(root,'hash-object',str(GEN))
        (E/(variant+'.patch')).write_text(git(root,'diff','--cached')+'\n')
        binary = configure_build(root, variant)
        for group in ('new','controls','handoff'):
            for ssa in ('-ffrontend-ssa','-fno-frontend-ssa'):
                for mode in ('none','mir-stack','fast','quality'):
                    name = variant+'-'+group+'-'+ssa.lstrip('-')+'-'+mode
                    output = E / name
                    status = run(name+'-compile',[binary,'cc','-target','x86_64-linux','-std=gnu17','-fwrapv','-fno-strict-aliasing','-funsigned-char','-g0',ssa,'-fregister-allocator='+mode,'-fverify-codegen',*([] if mode=='none' else ['-fno-machine-fallback']),E/(group+'.c'),'-o',output],root)
                    RESULTS[name] = {'compile':status, 'run':run(name+'-run',[output],root) if status==0 else 'not-run'}
            required(variant+'-'+group+'-object',[binary,'cc','-std=gnu17','-g0','-c',E/(group+'.c'),'-o',E/(variant+'-'+group+'.o')],root)
        if variant == 'local':
            RESULTS['local_suite'] = run('local-suite',[binary,'test','--ci=1','--verbose=1'],root,900)
        (E/'results.json').write_text(json.dumps(RESULTS,indent=2)+'\n')
    for group in ('new','controls','handoff'):
        RESULTS['object_equal_'+group] = (E/('local-'+group+'.o')).read_bytes() == (E/('structural-'+group+'.o')).read_bytes()
    RESULTS['performance'] = 'Correctness only on authorized hosted executor. No performance metrics, no 9700X, no desktop execution. Source identity equality to the prior shared candidate must be verified before carrying over its other evidence.'
    failures = [name for name,value in RESULTS.items() if name.startswith(('local-new-','local-controls-','local-handoff-','structural-new-','structural-controls-','structural-handoff-')) and isinstance(value,dict) and (value.get('compile')!=0 or value.get('run')!=0)]
    if RESULTS.get('local_suite') != 0:
        failures.append('complete-local-suite')
    if failures:
        raise RuntimeError('comparison failed: '+repr(failures))

try:
    comparison_main()
finally:
    (E/'results.json').write_text(json.dumps(RESULTS,indent=2)+'\n')
    (E/'commands.json').write_text(json.dumps(RECORDS,indent=2)+'\n')
    hashes = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in E.iterdir() if p.is_file() and p.name!='sha256.json'}
    (E/'sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
