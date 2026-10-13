import sys
p=sys.argv[1]+'/src/buster/lib/compiler/frontend/c/c_gen.c'
s=open(p).read()
anchor='BUSTER_C_INTERNAL bool c_ir_lower_body_advance(CIntegerIrBuilder* builder, CIrLowerBodyState* state)\n{'
assert s.count(anchor)==1
marks=['tw1_call','tw1_true','tw1_bound','tw1_predefined','tw1_nosymbol','tw1_stmt_keyword','tw1_mismatch',
       'tw2_call','tw2_true','tw2_bound','tw2_predefined','tw2_nosymbol','tw2_stmt_keyword','tw2_mismatch',
       'label_call','label_shape','label_default','label_proven','label_true']
code=''.join('__attribute__((noinline)) static void census_%s(void) { __asm__ volatile(""); }\n'%m for m in marks)
code+='''
__attribute__((noinline)) static bool census_type_word(CIntegerIrBuilder* builder, CToken first, CEntityId entity, u32 site)
{
    bool answer = c_parse_type_word_for_dialect(c_token_spelling(builder->preprocess.spelling_base, first), builder->preprocess.dialect);
    bool token_answer = c_parse_type_word_for_dialect_token(builder->preprocess, first);
    bool predefined = first.symbol && builder->preprocess.symbols && first.symbol <= builder->preprocess.symbols->predefined_limit;
    bool stmt = c_token_in_well_known_set(builder->preprocess.spelling_base, first, C_IR_STATEMENT_KEYWORD_SET);
    if (site == 1)
    {
        census_tw1_call();
        if (answer) census_tw1_true();
        if (entity.value != C_ID_UNDERLYING_INVALID) census_tw1_bound();
        if (predefined) census_tw1_predefined();
        if (!first.symbol) census_tw1_nosymbol();
        if (stmt) census_tw1_stmt_keyword();
        if (answer != token_answer) census_tw1_mismatch();
    }
    else
    {
        census_tw2_call();
        if (answer) census_tw2_true();
        if (entity.value != C_ID_UNDERLYING_INVALID) census_tw2_bound();
        if (predefined) census_tw2_predefined();
        if (!first.symbol) census_tw2_nosymbol();
        if (stmt) census_tw2_stmt_keyword();
        if (answer != token_answer) census_tw2_mismatch();
    }
    return answer;
}

__attribute__((noinline)) static bool census_label(CIntegerIrBuilder* builder, u32 start, u32 index, u32 end)
{
    CPreprocessResult const* preprocess = &builder->preprocess;
    census_label_call();
    bool shape = end <= preprocess->token_count && index < end && index + 1 < end && preprocess->tokens[index].kind == C_TOKEN_IDENTIFIER &&
                 preprocess->tokens[index + 1].punctuator == C_PUNCTUATOR_COLON;
    if (shape) census_label_shape();
    if (shape && c_token_is_well_known(preprocess->spelling_base, preprocess->tokens[index], C_SYMBOL_WELL_KNOWN_DEFAULT)) census_label_default();
    bool proven = c_ir_named_label_at(preprocess, start, index, end);
    if (proven) census_label_proven();
    bool result = proven && c_parse_label_candidate_at(&builder->parse, preprocess, start, index);
    if (result) census_label_true();
    return result;
}

'''
s=s.replace(anchor, code+anchor)
a='''                   c_parse_type_word_for_dialect(c_token_spelling(builder->preprocess.spelling_base, first), builder->preprocess.dialect))));'''
assert s.count(a)==1
s=s.replace(a,'''                   census_type_word(builder, first, first_entity, 1))));''')
b='''                                                     c_parse_type_word_for_dialect(c_token_spelling(builder->preprocess.spelling_base, first), builder->preprocess.dialect) ||
                                                      first_is_typedef_name'''
assert s.count(b)==1
s=s.replace(b,'''                                                     census_type_word(builder, first, first_entity, 2) ||
                                                      first_is_typedef_name''')
c='''            if (c_ir_named_label_at(&builder->preprocess, task.start, index, task.end) &&
                c_parse_label_candidate_at(&builder->parse, &builder->preprocess, task.start, index))
            {
                CIrLabel* label = c_ir_label_find(labels, label_count, &state->label_index, c_token_spelling(builder->preprocess.spelling_base, first));'''
assert s.count(c)==1
s=s.replace(c,'''            if (census_label(builder, task.start, index, task.end))
            {
                CIrLabel* label = c_ir_label_find(labels, label_count, &state->label_index, c_token_spelling(builder->preprocess.spelling_base, first));''')
open(p,'w').write(s)
