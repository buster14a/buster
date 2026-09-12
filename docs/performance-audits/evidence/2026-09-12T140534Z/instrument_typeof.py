from pathlib import Path
import json,re,subprocess,shlex,sys
root=Path.cwd(); out=root.parent/'typeof-diagnostic'; out.mkdir(exist_ok=True)
compile_entry=next(e for e in json.loads((root/'build/compile_commands.json').read_text()) if e['file'].endswith('/c_parse.c') and 'Debug' in e['command'])
for variant in (sys.argv[1:] or ['baseline','patch']):
 s=subprocess.check_output(['git','show','719ba64e3022a0ae1db078ea97cb951a70ac02a0:src/buster/lib/compiler/frontend/c/c_parse.c'],text=True) if variant=='baseline' else (root/'src/buster/lib/compiler/frontend/c/c_parse.c').read_text()
 s=s.replace('#include "c_internal.h"','#include "c_internal.h"\nBUSTER_GLOBAL_LOCAL u64 issue255_direct_work;\nBUSTER_GLOBAL_LOCAL u64 issue255_task_work;\nBUSTER_GLOBAL_LOCAL u64 issue255_scratch_requests;')
 first='BUSTER_C_INTERNAL bool c_parse_direct_expression_type(' if variant=='baseline' else 'BUSTER_GLOBAL_LOCAL CTypeId c_parse_direct_expression_base('
 a=s.index(first); b=s.index('BUSTER_C_INTERNAL u32 c_parse_matching_delimiter(',a)
 part=s[a:b]
 # Count actual expression-loop iterations, including normalization and unwind.
 part=re.sub(r'((?:while|for) \([^{}]*\)\n    *\{)',r'\1\n        issue255_direct_work += 1;',part)
 if variant=='baseline':
  part=part.replace('    u8* prefix_operators =', '    issue255_scratch_requests += end - start + 1;\n    u8* prefix_operators =',1)
 else:
  part=part.replace('    u32 frame_count = 0;', '    issue255_scratch_requests += (u64)capacity * (sizeof(CParseDirectExpressionFrame) + sizeof(u8));\n    u32 frame_count = 0;',1)
 s=s[:a]+part+s[b:]
 a=s.index('BUSTER_C_INTERNAL void c_type_parse_sizeof_step(');b=s.index('BUSTER_C_INTERNAL bool c_parse_expression_type_query(',a)
 part=s[a:b]
 part=re.sub(r'((?:while|for) \([^{}]*\)\n    *\{)',r'\1\n        issue255_task_work += 1;',part)
 s=s[:a]+part+s[b:]
 a=s.index('BUSTER_C_INTERNAL CAnalysisResult c_analyze_semantics('); b=s.index('\nCParseResult c_parse(',a)
 part=s[a:b]
 start=part.index('\n{')+2
 part=part[:start]+'''\n    u64 issue255_mark = arena->position;
    issue255_direct_work = 0;
    issue255_task_work = 0;
    issue255_scratch_requests = 0;
'''+part[start:]
 part=part[::-1].replace('    return result;'[::-1], '''    string_print(S8("TYPEOF_WORK tokens={u32} direct_loops={u64} task_loops={u64} scratch_requests={u64} persistent_bytes={u64} machine_bytes={u64} diagnostics={u32}\\n"),
                 preprocess.token_count, issue255_direct_work, issue255_task_work, issue255_scratch_requests,
                 arena->position - issue255_mark, machine_buffer_size, result.diagnostic_count);
    return result;'''[::-1],1)[::-1]
 s=s[:a]+part+s[b:]
 if variant=='baseline':
  s+='''\n#if BUSTER_INCLUDE_TESTS
bool c_test_parse_direct_expression_type(Arena* scratch, CPreprocessResult preprocess, CParseResult* result, u32 start, u32 end, CTypeId* type_out)
{
    return c_parse_direct_expression_type(scratch, preprocess, result, (CScopeId){.value = 0}, start, end, type_out);
}
#endif
'''
 source=out/(variant+'-c_parse.c');source.write_text(s); obj=out/(variant+'.o');binary=out/(variant+'-ide')
 args=shlex.split(compile_entry['command']); new=[];i=0
 while i<len(args):
  if args[i] in ['-MD']:i+=1;continue
  if args[i] in ['-MT','-MF','-o']:i+=2;continue
  new.append(str(source) if args[i]==compile_entry['file'] else args[i]);i+=1
 new+=['-I'+str(root/'src/buster/lib/compiler/frontend/c'),'-o',str(obj)]
 subprocess.run(new,cwd=compile_entry['directory'],check=True)
 link=shlex.split(subprocess.check_output(['ninja','-C','build','-f','build-Debug.ninja','-t','commands','Debug/ide'],text=True).splitlines()[-1])[2:-2]
 link=[str(obj) if x.endswith('/c_parse.c.o') else x for x in link]
 link[link.index('-o')+1]=str(binary)
 subprocess.run(link,cwd=root/'build',check=True)
 print(variant, binary,flush=True)
