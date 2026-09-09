from pathlib import Path
import json,shlex,subprocess
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('--repo',type=Path,default=Path.cwd())
parser.add_argument('--work',type=Path,required=True)
options=parser.parse_args()
repo=options.repo.resolve()
work=options.work.resolve()
source=(repo/'src/buster/apps/ide/ide.c').read_text()
needle='    CompilerDriverResult compile = compiler_driver_execute_invocation(arena, invocation);'
assert source.count(needle)==1
source=source.replace(needle,'''    invocation.suppress_diagnostic_records = string_equal(os_get_environment_variable(S8("BUSTER_DIAGNOSTIC_PROBE_SUPPRESS")), S8("1"));
'''+needle+'''
    if (string_equal(os_get_environment_variable(S8("BUSTER_DIAGNOSTIC_PROBE_REPORT")), S8("1")))
    {
        string_print(S8("DIAGNOSTIC_PROBE records={u32}\\n"), compile.diagnostic_count);
    }
''')
probe=work/'35-diagnostic-probe.c';probe.write_text(source)
rows=json.loads((repo/'build/compile_commands.json').read_text())
row=next(x for x in rows if '/apps/ide/ide.c' in x['file'] and '-DCMAKE_INTDIR=\\"Release\\"' in x['command'])
command=shlex.split(row['command'])
command[command.index('-o')+1]=str(work/'35-diagnostic-probe.o')
command[-1]=str(probe)
subprocess.run(command,cwd=row['directory'],check=True)
# The Release application is a unity build; retain the exact generated link
# command, substituting only its application object and output path.
r=subprocess.run(['ninja','-f','build-Release.ninja','-t','commands','Release/ide'],cwd=repo/'build',text=True,capture_output=True,check=True)
link=next(x for x in reversed(r.stdout.splitlines()) if ' -o Release/ide ' in x)
assert link.startswith(': && ') and link.endswith(' && :')
args=shlex.split(link[5:-5]);args[args.index('-o')+1]=str(work/'35-diagnostic-probe')
args=[str(work/'35-diagnostic-probe.o') if x=='CMakeFiles/ide.dir/Release/src/buster/apps/ide/ide.c.o' else x for x in args]
subprocess.run(args,cwd=repo/'build',check=True)
(work/'35-probe-build.json').write_text(json.dumps({'compile':command,'link':args,'source':str(probe)},indent=2))
