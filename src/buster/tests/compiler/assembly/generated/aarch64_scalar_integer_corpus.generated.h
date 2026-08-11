/* Generated from the checked-in Arm XML projection with llvm-mc 22.1.8. */
#ifndef BUSTER_AARCH64_SCALAR_INTEGER_CORPUS_GENERATED_H
#define BUSTER_AARCH64_SCALAR_INTEGER_CORPUS_GENERATED_H

typedef struct BusterAarch64ScalarIntegerCorpusCase BusterAarch64ScalarIntegerCorpusCase;
struct BusterAarch64ScalarIntegerCorpusCase
{
    String8 source;
    u32 word;
    String8 arm_encoding_name;
};

static const BusterAarch64ScalarIntegerCorpusCase buster_aarch64_scalar_integer_corpus[] = {
    {S8_INITIALIZER("adds wzr, wsp, w2, uxtw #1"), UINT32_C(0x2b2247ff), S8_INITIALIZER("ADDS_32S_addsub_ext")},
    {S8_INITIALIZER("adds wzr, wsp, #123"), UINT32_C(0x3101efff), S8_INITIALIZER("ADDS_32S_addsub_imm")},
    {S8_INITIALIZER("adds wzr, w1, w2, lsl #3"), UINT32_C(0x2b020c3f), S8_INITIALIZER("ADDS_32_addsub_shift")},
    {S8_INITIALIZER("adds xzr, sp, w2, uxtw #1"), UINT32_C(0xab2247ff), S8_INITIALIZER("ADDS_64S_addsub_ext")},
    {S8_INITIALIZER("adds xzr, sp, #123"), UINT32_C(0xb101efff), S8_INITIALIZER("ADDS_64S_addsub_imm")},
    {S8_INITIALIZER("adds xzr, x1, x2, lsl #3"), UINT32_C(0xab020c3f), S8_INITIALIZER("ADDS_64_addsub_shift")},
    {S8_INITIALIZER("add wsp, wsp, w2, uxtw #1"), UINT32_C(0x0b2247ff), S8_INITIALIZER("ADD_32_addsub_ext")},
    {S8_INITIALIZER("add wsp, wsp, #123"), UINT32_C(0x1101efff), S8_INITIALIZER("ADD_32_addsub_imm")},
    {S8_INITIALIZER("add wzr, w1, w2, lsl #3"), UINT32_C(0x0b020c3f), S8_INITIALIZER("ADD_32_addsub_shift")},
    {S8_INITIALIZER("add sp, sp, w2, uxtw #1"), UINT32_C(0x8b2247ff), S8_INITIALIZER("ADD_64_addsub_ext")},
    {S8_INITIALIZER("add sp, sp, #123"), UINT32_C(0x9101efff), S8_INITIALIZER("ADD_64_addsub_imm")},
    {S8_INITIALIZER("add xzr, x1, x2, lsl #3"), UINT32_C(0x8b020c3f), S8_INITIALIZER("ADD_64_addsub_shift")},
    {S8_INITIALIZER("ands wzr, w1, #0xff"), UINT32_C(0x72001c3f), S8_INITIALIZER("ANDS_32S_log_imm")},
    {S8_INITIALIZER("ands wzr, w1, w2, lsr #3"), UINT32_C(0x6a420c3f), S8_INITIALIZER("ANDS_32_log_shift")},
    {S8_INITIALIZER("ands xzr, x1, #0xff"), UINT32_C(0xf2401c3f), S8_INITIALIZER("ANDS_64S_log_imm")},
    {S8_INITIALIZER("ands xzr, x1, x2, lsr #3"), UINT32_C(0xea420c3f), S8_INITIALIZER("ANDS_64_log_shift")},
    {S8_INITIALIZER("and wsp, w1, #0xff"), UINT32_C(0x12001c3f), S8_INITIALIZER("AND_32_log_imm")},
    {S8_INITIALIZER("and wzr, w1, w2, lsr #3"), UINT32_C(0x0a420c3f), S8_INITIALIZER("AND_32_log_shift")},
    {S8_INITIALIZER("and sp, x1, #0xff"), UINT32_C(0x92401c3f), S8_INITIALIZER("AND_64_log_imm")},
    {S8_INITIALIZER("and xzr, x1, x2, lsr #3"), UINT32_C(0x8a420c3f), S8_INITIALIZER("AND_64_log_shift")},
    {S8_INITIALIZER("bfm wzr, w1, #3, #12"), UINT32_C(0x3303303f), S8_INITIALIZER("BFM_32M_bitfield")},
    {S8_INITIALIZER("bfm xzr, x1, #3, #12"), UINT32_C(0xb343303f), S8_INITIALIZER("BFM_64M_bitfield")},
    {S8_INITIALIZER("bics wzr, w1, w2, lsr #3"), UINT32_C(0x6a620c3f), S8_INITIALIZER("BICS_32_log_shift")},
    {S8_INITIALIZER("bics xzr, x1, x2, lsr #3"), UINT32_C(0xea620c3f), S8_INITIALIZER("BICS_64_log_shift")},
    {S8_INITIALIZER("bic wzr, w1, w2, lsr #3"), UINT32_C(0x0a620c3f), S8_INITIALIZER("BIC_32_log_shift")},
    {S8_INITIALIZER("bic xzr, x1, x2, lsr #3"), UINT32_C(0x8a620c3f), S8_INITIALIZER("BIC_64_log_shift")},
    {S8_INITIALIZER("ccmn w1, #7, #5, eq"), UINT32_C(0x3a470825), S8_INITIALIZER("CCMN_32_condcmp_imm")},
    {S8_INITIALIZER("ccmn w1, w2, #5, eq"), UINT32_C(0x3a420025), S8_INITIALIZER("CCMN_32_condcmp_reg")},
    {S8_INITIALIZER("ccmn x1, #7, #5, eq"), UINT32_C(0xba470825), S8_INITIALIZER("CCMN_64_condcmp_imm")},
    {S8_INITIALIZER("ccmn x1, x2, #5, eq"), UINT32_C(0xba420025), S8_INITIALIZER("CCMN_64_condcmp_reg")},
    {S8_INITIALIZER("ccmp w1, #7, #5, eq"), UINT32_C(0x7a470825), S8_INITIALIZER("CCMP_32_condcmp_imm")},
    {S8_INITIALIZER("ccmp w1, w2, #5, eq"), UINT32_C(0x7a420025), S8_INITIALIZER("CCMP_32_condcmp_reg")},
    {S8_INITIALIZER("ccmp x1, #7, #5, eq"), UINT32_C(0xfa470825), S8_INITIALIZER("CCMP_64_condcmp_imm")},
    {S8_INITIALIZER("ccmp x1, x2, #5, eq"), UINT32_C(0xfa420025), S8_INITIALIZER("CCMP_64_condcmp_reg")},
    {S8_INITIALIZER("eon wzr, w1, w2, lsr #3"), UINT32_C(0x4a620c3f), S8_INITIALIZER("EON_32_log_shift")},
    {S8_INITIALIZER("eon xzr, x1, x2, lsr #3"), UINT32_C(0xca620c3f), S8_INITIALIZER("EON_64_log_shift")},
    {S8_INITIALIZER("eor wsp, w1, #0xff"), UINT32_C(0x52001c3f), S8_INITIALIZER("EOR_32_log_imm")},
    {S8_INITIALIZER("eor wzr, w1, w2, lsr #3"), UINT32_C(0x4a420c3f), S8_INITIALIZER("EOR_32_log_shift")},
    {S8_INITIALIZER("eor sp, x1, #0xff"), UINT32_C(0xd2401c3f), S8_INITIALIZER("EOR_64_log_imm")},
    {S8_INITIALIZER("eor xzr, x1, x2, lsr #3"), UINT32_C(0xca420c3f), S8_INITIALIZER("EOR_64_log_shift")},
    {S8_INITIALIZER("extr wzr, w1, w2, #3"), UINT32_C(0x13820c3f), S8_INITIALIZER("EXTR_32_extract")},
    {S8_INITIALIZER("extr xzr, x1, x2, #3"), UINT32_C(0x93c20c3f), S8_INITIALIZER("EXTR_64_extract")},
    {S8_INITIALIZER("movk w0, #0x1234"), UINT32_C(0x72824680), S8_INITIALIZER("MOVK_32_movewide")},
    {S8_INITIALIZER("movk x0, #0x1234"), UINT32_C(0xf2824680), S8_INITIALIZER("MOVK_64_movewide")},
    {S8_INITIALIZER("movn w0, #0x1234"), UINT32_C(0x12824680), S8_INITIALIZER("MOVN_32_movewide")},
    {S8_INITIALIZER("movn x0, #0x1234"), UINT32_C(0x92824680), S8_INITIALIZER("MOVN_64_movewide")},
    {S8_INITIALIZER("movz w0, #0x1234"), UINT32_C(0x52824680), S8_INITIALIZER("MOVZ_32_movewide")},
    {S8_INITIALIZER("movz x0, #0x1234"), UINT32_C(0xd2824680), S8_INITIALIZER("MOVZ_64_movewide")},
    {S8_INITIALIZER("orn wzr, w1, w2, lsr #3"), UINT32_C(0x2a620c3f), S8_INITIALIZER("ORN_32_log_shift")},
    {S8_INITIALIZER("orn xzr, x1, x2, lsr #3"), UINT32_C(0xaa620c3f), S8_INITIALIZER("ORN_64_log_shift")},
    {S8_INITIALIZER("orr wsp, w1, #0xff"), UINT32_C(0x32001c3f), S8_INITIALIZER("ORR_32_log_imm")},
    {S8_INITIALIZER("orr wzr, w1, w2, lsr #3"), UINT32_C(0x2a420c3f), S8_INITIALIZER("ORR_32_log_shift")},
    {S8_INITIALIZER("orr sp, x1, #0xff"), UINT32_C(0xb2401c3f), S8_INITIALIZER("ORR_64_log_imm")},
    {S8_INITIALIZER("orr xzr, x1, x2, lsr #3"), UINT32_C(0xaa420c3f), S8_INITIALIZER("ORR_64_log_shift")},
    {S8_INITIALIZER("rmif x1, #3, #5"), UINT32_C(0xba018425), S8_INITIALIZER("RMIF_only_rmif")},
    {S8_INITIALIZER("sbfm wzr, w1, #3, #12"), UINT32_C(0x1303303f), S8_INITIALIZER("SBFM_32M_bitfield")},
    {S8_INITIALIZER("sbfm xzr, x1, #3, #12"), UINT32_C(0x9343303f), S8_INITIALIZER("SBFM_64M_bitfield")},
    {S8_INITIALIZER("subs wzr, wsp, w2, uxtw #1"), UINT32_C(0x6b2247ff), S8_INITIALIZER("SUBS_32S_addsub_ext")},
    {S8_INITIALIZER("subs wzr, wsp, #123"), UINT32_C(0x7101efff), S8_INITIALIZER("SUBS_32S_addsub_imm")},
    {S8_INITIALIZER("subs wzr, w1, w2, lsl #3"), UINT32_C(0x6b020c3f), S8_INITIALIZER("SUBS_32_addsub_shift")},
    {S8_INITIALIZER("subs xzr, sp, w2, uxtw #1"), UINT32_C(0xeb2247ff), S8_INITIALIZER("SUBS_64S_addsub_ext")},
    {S8_INITIALIZER("subs xzr, sp, #123"), UINT32_C(0xf101efff), S8_INITIALIZER("SUBS_64S_addsub_imm")},
    {S8_INITIALIZER("subs xzr, x1, x2, lsl #3"), UINT32_C(0xeb020c3f), S8_INITIALIZER("SUBS_64_addsub_shift")},
    {S8_INITIALIZER("sub wsp, wsp, w2, uxtw #1"), UINT32_C(0x4b2247ff), S8_INITIALIZER("SUB_32_addsub_ext")},
    {S8_INITIALIZER("sub wsp, wsp, #123"), UINT32_C(0x5101efff), S8_INITIALIZER("SUB_32_addsub_imm")},
    {S8_INITIALIZER("sub wzr, w1, w2, lsl #3"), UINT32_C(0x4b020c3f), S8_INITIALIZER("SUB_32_addsub_shift")},
    {S8_INITIALIZER("sub sp, sp, w2, uxtw #1"), UINT32_C(0xcb2247ff), S8_INITIALIZER("SUB_64_addsub_ext")},
    {S8_INITIALIZER("sub sp, sp, #123"), UINT32_C(0xd101efff), S8_INITIALIZER("SUB_64_addsub_imm")},
    {S8_INITIALIZER("sub xzr, x1, x2, lsl #3"), UINT32_C(0xcb020c3f), S8_INITIALIZER("SUB_64_addsub_shift")},
    {S8_INITIALIZER("ubfm wzr, w1, #3, #12"), UINT32_C(0x5303303f), S8_INITIALIZER("UBFM_32M_bitfield")},
    {S8_INITIALIZER("ubfm xzr, x1, #3, #12"), UINT32_C(0xd343303f), S8_INITIALIZER("UBFM_64M_bitfield")},
    {S8_INITIALIZER("udf #0x1234"), UINT32_C(0x00001234), S8_INITIALIZER("UDF_only_perm_undef")},

};

#define BUSTER_AARCH64_SCALAR_INTEGER_CORPUS_COUNT 72u
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(buster_aarch64_scalar_integer_corpus) == BUSTER_AARCH64_SCALAR_INTEGER_CORPUS_COUNT);

#endif
