"""Extract actual C scanners for diagnostic work counts and differential checks.

Run from the repository root. No surrogate Python classifier is used.
"""
import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--baseline', default='61a84e2bc916f8d6efae3125d68e8116dc2f9189')
parser.add_argument('--cc', default='clang-18')
parser.add_argument('--candidate', default='src/buster/tests/compiler/codegen/machine_test.c')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--output', required=True, type=pathlib.Path)
args = parser.parse_args()
path = 'src/buster/tests/compiler/codegen/machine_test.c'
base = subprocess.check_output(['git', 'show', f'{args.baseline}:{path}'], text=True)
candidate = pathlib.Path(args.candidate).read_text()

def functions(text):
    return text[text.index('BUSTER_GLOBAL_LOCAL bool machine_test_source_identifier_start'):text.index('BUSTER_GLOBAL_LOCAL MachineX64SourceAudit machine_test_x86_source_authority_audit')]

types = base[base.index('typedef struct MachineX64SourceSpan'):base.index('BUSTER_GLOBAL_LOCAL bool machine_test_source_identifier_start')]
old = functions(base).replace('machine_test_', 'old_')
new = functions(candidate).replace('machine_test_', 'new_')
# The fixture follows the candidate scanner and belongs in the in-process tests.
if 'BUSTER_GLOBAL_LOCAL UnitTestResult' in new:
    new = new[:new.index('BUSTER_GLOBAL_LOCAL UnitTestResult')]

def instrument(text, prefix):
    text = text.replace('if (!source.bytes || source.start', f'{prefix}_counts.tokens += 1;\n    if (!source.bytes || source.start', 1)
    text = text.replace('u64 segment_start = brace;', f'{prefix}_counts.braces += 1;\n    u64 segment_start = brace;')
    text = text.replace('x86_found |=', f'{prefix}_counts.markers += 1; x86_found |=')
    text = text.replace('aarch64_found |=', f'{prefix}_counts.markers += 1; aarch64_found |=')
    # Only the old prefix loop has this line. Counts loop iterations, not memcmp bytes.
    text = text.replace("if (source.bytes[offset] == '{')", f"{prefix}_counts.prefix += 1;\n        if (source.bytes[offset] == '{{')", 1)
    start = text.index(f'BUSTER_GLOBAL_LOCAL void {prefix}_source_scan_writers')
    head, scan = text[:start], text[start:]
    brace = scan.index('\n{') + 2
    scan = scan[:brace] + f'\n    {prefix}_counts.bodies += 1;\n    {prefix}_counts.body_bytes += body.end - body.start;' + scan[brace:]
    scan = scan.replace(f'if (!{prefix}_source_token_at', f'{prefix}_counts.recognition += 1;\n            if (!{prefix}_source_token_at')
    scan = scan.replace('if (arch == MACHINE_X64_SOURCE_ARCH_X86)', f'{prefix}_counts.hits += 1;\n            if (verify && arch != old_source_arch_for_writer(source, body, offset, default_arch)) abort();\n            if (arch == MACHINE_X64_SOURCE_ARCH_X86)')
    if prefix == 'new':
        scan = scan.replace('u8 byte = source.bytes[offset];', 'new_counts.walk += 1;\n        u8 byte = source.bytes[offset];')
        scan = scan.replace('while (end < body.end && new_source_identifier_continue(source.bytes[end])) end += 1;', 'while (end < body.end && new_source_identifier_continue(source.bytes[end])) { end += 1; new_counts.walk += 1; }')
    return head + scan

consumers = base.split('static MachineX64ConsumerSite const consumers[] = {', 1)[1].split('};', 1)[0]
sites = re.findall(r'S8_INITIALIZER\("([^"]+)"\).*S8_INITIALIZER\("([^"]+)"\)', consumers)
files = sorted(set(file for file, _ in sites))
site_entries = ',\n'.join(f'{{"{file}", "{owner}"}}' for file, owner in sites)
prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef char char8;
typedef struct { char* pointer; u64 length; } String8;
typedef struct { u8* pointer; u64 length; } ByteSlice;
typedef struct { int unused; } Arena;
#define arena_allocate(a,t,n) ((void)(a), (t*)calloc((size_t)(n), sizeof(t)))
#define BUSTER_GLOBAL_LOCAL static
#define BUSTER_ARRAY_LENGTH(a) (sizeof(a)/sizeof((a)[0]))
#define BUSTER_MAX(a,b) ((a)>(b)?(a):(b))
#define S8(s) ((String8){(char*)(s),sizeof(s)-1})
typedef struct { u64 bodies, body_bytes, hits, prefix, braces, markers, tokens, recognition, walk; } Counts;
static Counts old_counts, new_counts;
static bool verify;
'''
main = r'''
static void report(char const* label, Counts c)
{
    printf("%s bodies=%llu body_bytes=%llu hits=%llu prefix=%llu braces=%llu markers=%llu tokens=%llu recognition=%llu walk=%llu\n",
        label, (unsigned long long)c.bodies, (unsigned long long)c.body_bytes, (unsigned long long)c.hits,
        (unsigned long long)c.prefix, (unsigned long long)c.braces, (unsigned long long)c.markers,
        (unsigned long long)c.tokens, (unsigned long long)c.recognition, (unsigned long long)c.walk);
}
static void scan(MachineX64SourceSpan source, MachineX64SourceSpan body, MachineX64SourceArch arch, bool assembly)
{
    String8 codegen[] = {S8("codegen_emit_u8"), S8("codegen_emit_u32"), S8("codegen_emit_u64")};
    String8 assembler[] = {S8("assembly_emit_byte"), S8("assembly_emit_immediate")};
    String8* writers = assembly ? assembler : codegen;
    bool x=false, a=false, u=false, nx=false, na=false, nu=false;
    verify = false;
    old_source_scan_writers(source, body, arch, writers, assembly ? 2 : 3, &x,&a,&u);
    Counts before = old_counts;
    verify = true;
    new_source_scan_writers(source, body, arch, writers, assembly ? 2 : 3, &nx,&na,&nu);
    old_counts = before;
    assert(x==nx && a==na && u==nu);
}
static MachineX64SourceSpan read_source(char const* path)
{
    FILE* f=fopen(path,"rb"); assert(f);
    fseek(f,0,SEEK_END); long size=ftell(f); rewind(f); assert(size>=0);
    u8* bytes=malloc((size_t)size+1); assert(bytes);
    assert(fread(bytes,1,(size_t)size,f)==(size_t)size); fclose(f);
    u8* sanitized=old_source_sanitize(NULL,(ByteSlice){bytes,(u64)size}); free(bytes);
    return (MachineX64SourceSpan){sanitized,(u64)size,0,(u64)size};
}
int main(void)
{
    struct {char const* file; char const* owner;} sites[] = { SITE_ENTRIES };
    char const* files[] = { FILE_ENTRIES };
    for (u64 i=0; i<BUSTER_ARRAY_LENGTH(files); ++i)
    {
        MachineX64SourceSpan source=read_source(files[i]);
        bool assembly=strstr(files[i],"assembly/assembly.c")!=NULL;
        MachineX64SourceArch arch=strstr(files[i],"lib/x86_64.c") ? MACHINE_X64_SOURCE_ARCH_X86 : MACHINE_X64_SOURCE_ARCH_UNKNOWN;
        for (u64 j=0;j<BUSTER_ARRAY_LENGTH(sites);++j)
        {
            if(strcmp(files[i],sites[j].file)!=0) continue;
            MachineX64SourceSpan body={0}; String8 owner={(char*)sites[j].owner,strlen(sites[j].owner)};
            Counts saved=old_counts;
            assert(old_source_function_body(source,owner,&body)); old_counts=saved;
            scan(source,body,arch,assembly);
        }
        char const* prefixes[]={"codegen_canonical_x64_","x64_emit_","assembly_x86_"};
        for(u64 p=0;p<3;++p)
        {
            if ((p==2)!=assembly || (!assembly && !strstr(files[i],"codegen/codegen.c"))) continue;
            u64 length=strlen(prefixes[p]);
            for(u64 offset=0;offset+length<=source.end;++offset)
            {
                if(memcmp(source.bytes+offset,prefixes[p],length)!=0 || (offset && old_source_identifier_continue(source.bytes[offset-1]))) continue;
                u64 end=offset+length;
                while(end<source.end && old_source_identifier_continue(source.bytes[end])) ++end;
                String8 owner={(char*)source.bytes+offset,end-offset}; MachineX64SourceSpan body={0};
                Counts saved=old_counts;
                bool found=old_source_function_body_at(source,owner,offset,&body); old_counts=saved;
                if(found) scan(source,body,MACHINE_X64_SOURCE_ARCH_X86,assembly);
            }
        }
        free((void*)source.bytes);
    }
    report("repository_old",old_counts); report("repository_new",new_counts);
    char const* row="codegen_emit_u8(buffer, 0);\n";
    for(u64 n=256;n<=4096;n*=2)
    {
        u64 length=2+n*strlen(row); u8* bytes=calloc((size_t)length+1,1); bytes[0]='{';
        for(u64 i=0;i<n;++i) memcpy(bytes+1+i*strlen(row),row,strlen(row)); bytes[length-1]='}';
        MachineX64SourceSpan body={bytes,length,0,length}; old_counts=(Counts){0}; new_counts=(Counts){0};
        scan(body,body,MACHINE_X64_SOURCE_ARCH_X86,false);
        printf("generated n=%llu ",(unsigned long long)n); report("old",old_counts); report("new",new_counts); free(bytes);
    }
    // Seeded mixed syntax controls exercise legacy classification at every hit,
    // including unbalanced braces and the stack's 256-entry saturation rule.
    char const* fragments[]={"{", "}", ";", "if (arch == CPU_ARCH_X86_64) {", "if (arch == CPU_ARCH_AARCH64) {", "else {",
        "codegen_emit_u8(b,0);", "codegen_emit_u32(b,0)", "codegen_emit_u64(b,0)", "CPU_ARCH_X86_64 ? ", ": ", "CPU_ARCH_AARCH64 ? ",
        "CPU_ARCH_X86_64suffix ", "prefix_codegen_emit_u8(b,0);", "codegen_emit_u8_suffix(b,0);", "7codegen_emit_u8(b,0);"};
    uint32_t seed=1887;
    for(u32 trial=0;trial<128;++trial)
    {
        u8 bytes[65536]; u64 length=0;
        for(u32 i=0;i<512;++i) {seed=seed*1664525u+1013904223u; char const* text=fragments[(seed>>16)%16]; u64 n=strlen(text); memcpy(bytes+length,text,n); length+=n;}
        MachineX64SourceSpan body={bytes,length,0,length};
        for(u32 arch=0;arch<3;++arch) scan(body,body,(MachineX64SourceArch)arch,false);
    }
    puts("differential: every repository/generated/control hit matched");
    return 0;
}
'''
main = main.replace('SITE_ENTRIES', site_entries).replace('FILE_ENTRIES', ','.join(f'"{f}"' for f in files))
args.output.mkdir(parents=True, exist_ok=True)
cfile = args.output / 'scanner.c'
cfile.write_text(prelude + types + instrument(old, 'old') + instrument(new, 'new') + main)
binary = args.output / 'scanner'
command = [args.cc, '-std=c11', '-O2', '-g', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', str(cfile), '-o', str(binary)]
if args.sanitize:
    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
print('compile:', ' '.join(command), flush=True)
subprocess.run(command, check=True)
subprocess.run([str(binary.resolve())], check=True)
