import sys
root=sys.argv[1]
p=root+'/src/buster/lib/compiler/frontend/c/c_parse.c'
s=open(p).read()
old_decl='BUSTER_C_INTERNAL bool c_parse_type_word_for_dialect_token(CPreprocessResult preprocess, CToken token);\n'
assert s.count(old_decl)==1
s=s.replace(old_decl,'')
old_def='BUSTER_C_INTERNAL bool c_parse_type_word_for_dialect_token(CPreprocessResult preprocess, CToken token)\n{'
assert s.count(old_def)==1
s=s.replace(old_def,'BUSTER_C_SHARED bool c_parse_type_word_for_dialect_token(CPreprocessResult preprocess, CToken token)\n{')
open(p,'w').write(s)
h=root+'/src/buster/lib/compiler/frontend/c/c_internal.h'
t=open(h).read()
a='BUSTER_C_EXTERN bool c_parse_type_word_for_dialect(String8 spelling, CPreprocessDialect dialect);\n'
assert t.count(a)==1
t=t.replace(a,a+'BUSTER_C_EXTERN bool c_parse_type_word_for_dialect_token(CPreprocessResult preprocess, CToken token);\n')
open(h,'w').write(t)
