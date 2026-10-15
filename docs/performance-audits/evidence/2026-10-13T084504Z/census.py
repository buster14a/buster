# Writes the throwaway census instrumentation into <tree>/src/.../c_gen.c.
# Each remaining spelling-form c_parse_type_word_for_dialect site in c_gen.c
# is routed through a noinline census_type_word, whose noinline markers count
# calls, true answers, symbol-less tokens, and token-form disagreements.
import sys
p=sys.argv[1]+'/src/buster/lib/compiler/frontend/c/c_gen.c'
s=open(p).read()
call='c_parse_type_word_for_dialect(c_token_spelling(builder->preprocess.spelling_base, first), builder->preprocess.dialect)'
anchors=[('BUSTER_C_INTERNAL IrTypeId c_ir_type_name_prefix(CIntegerIrBuilder* builder, u32 start, u32 end, u32* index_out, CType* qualifiers_out)\n{',1),
         ('BUSTER_C_INTERNAL bool c_ir_statement_expression_tail(CIntegerIrBuilder* builder, u32 open, u32 close, u32* start_out, u32* end_out)\n{',2)]
marks=[]
for site in (1,2):
    for m in ('call','true','nosymbol','predefined','mismatch'):
        marks.append('tw%d_%s'%(site,m))
code=''.join('__attribute__((noinline)) static void census_%s(void) { __asm__ volatile(""); }\n'%m for m in marks)
code+='''
__attribute__((noinline)) static bool census_type_word(CIntegerIrBuilder* builder, CToken first, u32 site)
{
    bool answer = c_parse_type_word_for_dialect(c_token_spelling(builder->preprocess.spelling_base, first), builder->preprocess.dialect);
    bool token_answer = c_parse_type_word_for_dialect_token(builder->preprocess, first);
    bool predefined = first.symbol && builder->preprocess.symbols && first.symbol <= builder->preprocess.symbols->predefined_limit;
    if (site == 1)
    {
        census_tw1_call();
        if (answer) census_tw1_true();
        if (!first.symbol) census_tw1_nosymbol();
        if (predefined) census_tw1_predefined();
        if (answer != token_answer) census_tw1_mismatch();
    }
    else
    {
        census_tw2_call();
        if (answer) census_tw2_true();
        if (!first.symbol) census_tw2_nosymbol();
        if (predefined) census_tw2_predefined();
        if (answer != token_answer) census_tw2_mismatch();
    }
    return answer;
}

'''
first_anchor=anchors[0][0]
assert s.count(first_anchor)==1
s=s.replace(first_anchor, code+first_anchor)
for anchor,site in anchors:
    i=s.index(anchor); j=s.index('\n}\n',i)
    body=s[i:j]
    assert body.count(call)==1, (site, body.count(call))
    body=body.replace(call,'census_type_word(builder, first, %d)'%site)
    s=s[:i]+body+s[j:]
assert s.count(call)==2  # census_type_word itself and the c_ir_lower_body_advance reference check
open(p,'w').write(s)
