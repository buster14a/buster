#!/usr/bin/env python3
"""Hosted-only exact-helper differential; not an integration/timing verdict."""
import hashlib, json, os, pathlib, subprocess, tarfile
P=pathlib.Path
out=P('initjoin-falsification'); out.mkdir(exist_ok=True)
root=P('tools/investigations/initjoin-20260925')
b=b''.join((root/f'part{i}').read_bytes() for i in range(5))
assert hashlib.sha256(b).hexdigest()=='6fb5d22b306f8638ef7ea36f3e5f47b66a596305c5cddfd5780832dc063fb828'
(root/'bundle.tar.gz').write_bytes(b)
with tarfile.open(root/'bundle.tar.gz') as t: t.extractall(root, filter='data')
source=P('src/buster/lib/compiler/link/link.c')
def collect(text):
    a=text.index('BUSTER_GLOBAL_LOCAL u32 link_initializer_entries_collect(')
    return text[a:text.index('\n}\n',a)+3]
old=source.read_text(); baseline=collect(old).replace('link_initializer_entries_collect(', 'baseline_collect(',1)
patch=(root/'candidate.patch').read_bytes()
assert hashlib.sha256(patch).hexdigest()=='7534e5970e8e8a08456065714901b63388a5e97996dca2634ab6845c3fb8577a'
subprocess.run(['git','apply','--check',root/'candidate.patch'],check=True)
subprocess.run(['git','apply',root/'candidate.patch'],check=True)
new=source.read_text(); a=new.index('// Sparse arrays must not turn'); e=new.index('\n}\n',new.index('BUSTER_GLOBAL_LOCAL u32 link_initializer_entries_collect('))+3
candidate=new[a:e]
(out/'baseline-helper.c.txt').write_text(baseline)
(out/'candidate-helper.c.txt').write_text(candidate)
header=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t s64;
#define BUSTER_GLOBAL_LOCAL static
#define OBJECT_INITIALIZER_ENTRY_SIZE 8
typedef enum { OBJECT_SECTION_TEXT, OBJECT_SECTION_INIT_ARRAY, OBJECT_SECTION_FINI_ARRAY, OBJECT_SECTION_COUNT } ObjectSectionKind;
enum { OBJECT_RELOCATION_ABSOLUTE32, OBJECT_RELOCATION_ABSOLUTE64 };
typedef struct { s64 addend; u32 symbol; u32 reserved; } LinkInitializerEntry;
/* This isolated probe models only fields read by the exact helper. Full
   repository headers, ABI/layout, and integration are tested by the main job. */
typedef struct { u64 offset; s64 addend; u32 symbol, section, kind; } ObjectRelocation;
typedef struct { struct { u8 *pointer; u64 length; } data; } ObjectSection;
typedef struct { ObjectSection *sections; ObjectRelocation *relocations; u32 relocation_count; } ObjectFile;
'''
checks=r'''
static u64 rng=UINT64_C(0x46a5ba182becd491);
static u64 next_value(void) { rng=rng*UINT64_C(6364136223846793005)+1; return rng; }
static void require(bool yes, u32 e, u32 r, u32 trial, const char *what) {
 if (!yes) { fprintf(stderr,"FAIL E=%u R=%u trial=%u %s\n",e,r,trial,what); exit(1); }
}
/* Separate-index design: simple linear alternative, with its extra E*4 bytes. */
static u32 indexed(ObjectFile *o,ObjectSectionKind kind,LinkInitializerEntry *out,bool rev) {
 u64 e=o->sections[kind].data.length/8; u32 *index=malloc((e?e:1)*sizeof(*index));
 if(!index) exit(2);
 for(u64 i=0;i<e;i++) index[i]=UINT32_MAX;
 for(u32 i=0;i<o->relocation_count;i++) {
  ObjectRelocation x=o->relocations[i]; u64 slot=x.offset/8;
  if(x.section==(u32)kind && x.kind==OBJECT_RELOCATION_ABSOLUTE64 && !(x.offset%8) && slot<e && index[slot]==UINT32_MAX) index[slot]=i;
 }
 u32 n=0;
 for(u64 p=0;p<e;p++) { u64 slot=rev?e-1-p:p;
  if(index[slot]!=UINT32_MAX) { ObjectRelocation x=o->relocations[index[slot]]; out[n++]=(LinkInitializerEntry){.addend=x.addend,.symbol=x.symbol}; }
 }
 free(index); return n;
}
static u64 rows;
static void check(u32 e,u32 r,u32 trial,u32 shape) {
 ObjectRelocation *rel=malloc((r?r:1)*sizeof(*rel)), *saved=malloc((r?r:1)*sizeof(*rel));
 LinkInitializerEntry *base=malloc((e+2)*sizeof(*base)), *cand=malloc((e+2)*sizeof(*cand)), *alt=malloc((e+2)*sizeof(*alt));
 if(!rel||!saved||!base||!cand||!alt) exit(2);
 ObjectSection sections[OBJECT_SECTION_COUNT]={0};
 for(u32 kind=OBJECT_SECTION_INIT_ARRAY;kind<=OBJECT_SECTION_FINI_ARRAY;kind++) {
  sections[kind].data.length=(u64)e*8+trial%8;
  for(u32 i=0;i<r;i++) {
   u64 x=next_value(); u64 slot=e?next_value()%e:0;
   rel[i]=(ObjectRelocation){.offset=slot*8,.addend=-(s64)(x&INT64_MAX),.symbol=(u32)(x>>32),.section=kind,.kind=OBJECT_RELOCATION_ABSOLUTE64};
   if(shape==0) { switch((x>>16)%9) {
    case 0: rel[i].offset=UINT64_MAX; break;
    case 1: rel[i].offset=(u64)e*8; break;
    case 2: rel[i].offset=slot*8+1; break;
    case 3: rel[i].section=OBJECT_SECTION_TEXT; break;
    case 4: rel[i].kind=OBJECT_RELOCATION_ABSOLUTE32; break;
    default: break;
   }} else if(shape==1) rel[i].offset=e?(u64)(e-1-i%e)*8:0;
   else rel[i].offset=e?(u64)(i%2?0:e-1)*8:0;
   if(i && i%7==0) { rel[i]=rel[i-1]; rel[i].symbol=UINT32_MAX; rel[i].addend=INT64_MIN; }
  }
  memcpy(saved,rel,r*sizeof(*rel));
  ObjectFile o={.sections=sections,.relocations=rel,.relocation_count=r};
  memset(cand,0xa5,(e+2)*sizeof(*cand)); LinkInitializerEntry guard=cand[0];
  for(u32 reverse=0;reverse<2;reverse++) {
   u32 bn=baseline_collect(&o,(ObjectSectionKind)kind,base,reverse!=0);
   u32 cn=link_initializer_entries_collect(&o,(ObjectSectionKind)kind,cand+1,reverse!=0);
   u32 an=indexed(&o,(ObjectSectionKind)kind,alt,reverse!=0);
   require(bn==cn && cn==an && cn<=e,e,r,trial,"count");
   require(!memcmp(base,cand+1,cn*sizeof(*base)),e,r,trial,"incumbent bytes");
   require(!memcmp(base,alt,cn*sizeof(*base)),e,r,trial,"index bytes");
   require(!memcmp(cand,&guard,sizeof(guard)) && !memcmp(cand+e+1,&guard,sizeof(guard)),e,r,trial,"canaries");
   require(!memcmp(saved,rel,r*sizeof(*rel)),e,r,trial,"immutable input");
   rows++;
  }
  o.relocation_count=0;
  require(link_initializer_entries_collect(&o,(ObjectSectionKind)kind,0,false)==0,e,r,trial,"empty no touch");
 }
 free(rel);free(saved);free(base);free(cand);free(alt);
}
int main(void) {
 u32 sizes[]={0,1,2,3,7,31,32,33,63,64,65,127,128,129,257,4097,65537};
 for(u32 s=0;s<sizeof(sizes)/sizeof(sizes[0]);s++) {
  u32 e=sizes[s], limit=e>257?4:24;
  for(u32 t=0;t<limit;t++) check(e,(u32)(next_value()%(e>257?65:3*e+9)),t,0);
  u32 cap=e/32;
  for(u32 shape=1;shape<3;shape++) {
   check(e,0,0,shape); check(e,1,1,shape);
   if(e<=4097) { check(e,cap,2,shape);check(e,cap+1,3,shape);check(e,cap+2,4,shape); }
  }
 }
 printf("PASS rows=%llu deterministic_seed=46a5ba182becd491\n",(unsigned long long)rows);
 return 0;
}
'''
(out/'probe.c').write_text(header+baseline+candidate+checks)
records=[]
for cc in ['clang','gcc']:
 for mode in ['release','sanitized']:
  binary=out/f'{cc}-{mode}'
  flags=['-O2'] if mode=='release' else ['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
  commands=[[cc,'-std=c17','-Wall','-Wextra','-Werror','-fwrapv','-fno-strict-aliasing','-funsigned-char',*flags,str(out/'probe.c'),'-o',str(binary)],[str(binary)]]
  for index,argv in enumerate(commands):
   record={'argv':argv,'compiler':cc,'mode':mode,'step':index}
   name=out/f'{cc}-{mode}-{index}'
   with open(str(name)+'.stdout','wb') as so,open(str(name)+'.stderr','wb') as se:
    try: p=subprocess.run(argv,stdout=so,stderr=se,timeout=180,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1'),check=False); record['exit_code']=p.returncode
    except subprocess.TimeoutExpired: record['exit_code']=None;record['timeout']=True
   records.append(record);print(record,flush=True)
   if record['exit_code']!=0: break
(out/'attempts.json').write_text(json.dumps(records,indent=2)+'\n')
(out/'identities.json').write_text(json.dumps({'baseline_commit':'ade6ac4b6ecb21f30b61b656439bac476c145e2f','transport_commit':os.getenv('GITHUB_SHA'),'patch_sha256':hashlib.sha256(patch).hexdigest(),'old_link_sha256':hashlib.sha256(old.encode()).hexdigest(),'candidate_link_sha256':hashlib.sha256(new.encode()).hexdigest()},indent=2)+'\n')
manifest={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file()}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
assert len(records)==8 and all(r['exit_code']==0 for r in records),'probe failed; raw attempts retained'
