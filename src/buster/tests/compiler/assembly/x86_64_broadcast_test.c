// Included by x86_64_metadata_test.c; shares its byte and physical-query helpers.
// Golden bytes were assembled with GNU as 2.44: as --64 input.s -o input.o;
// objcopy -O binary --only-section=.text input.o input.bin. Source contains each
// instruction between labels, so the oracle does not depend on Buster decoding.
// Full tuples cover f32/f64 and integer elements, all vector lengths, both signed
// disp8 limits, nonmultiples, disp32 fallbacks, SIB, RBP/R13 and address-size 32.
// Test-only plan publication runs before any module can start worker lanes.
// Broadcast is not currently part of the narrow machine-token query policy,
// so production machine prewarm need not reserve these exact plans.
void x86_64_metadata_broadcast_prewarm(void)
{
    buster_x86_metadata_prewarm();
    String8 mnemonics[] = {S8("VADDPS"), S8("VADDPD"), S8("VPADDD"), S8("VPADDQ"), S8("VFMADD132PS"), S8("VFMADD132PD")};
    String8 features[] = {S8("avx512f"), S8("avx512vl")};
    for (u32 opcode = 0; opcode < BUSTER_ARRAY_LENGTH(mnemonics); opcode += 1)
    {
        for (u16 width = 128; width <= 512; width *= 2)
        {
            u8 reg_class = width == 128 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM
                             : width == 256 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM
                                            : BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM;
            u16 element = opcode & 1 ? 64 : 32;
            BusterX86MetadataPhysicalOperand operands[] = {
                x86_64_metadata_test_physical_reg(reg_class, 2, width),
                x86_64_metadata_test_physical_reg(reg_class, 1, width),
                x86_64_metadata_test_physical_mem_base(0, element, 0),
            };
            BusterX86MetadataPhysicalQuery physical = x86_64_metadata_test_physical_query(
                mnemonics[opcode], operands, BUSTER_ARRAY_LENGTH(operands),
                (BusterX86MetadataPhysicalAttributes){.decorator_flags = BUSTER_X86_METADATA_DECORATOR_BROADCAST,
                                                      .broadcast_elements = (u8)(width / element)},
                features, BUSTER_ARRAY_LENGTH(features));
            BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(physical);
            BusterX86MetadataFormKey key = {0};
            BusterX86MetadataExactPlan plan = {0};
            if (selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && buster_x86_metadata_form_key(selected.form_id, &key))
            {
                // Failure is reported by the ordinary regression assertion.
                (void)buster_x86_metadata_exact_plan_prepare(key, &plan);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL UnitTestResult x86_64_metadata_broadcast_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct BroadcastCase
    {
        String8 mnemonic;
        String8 source;
        u16 vector_bits;
        u16 element_bits;
        s32 displacement;
        u8 base;
        u8 index;
        u8 scale;
        u8 address_size;
        u8 byte_count;
        u8 bytes[16];
    } cases[] = {
        {S8("VADDPS"), S8("vaddps 0(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x10}},
        {S8("VADDPS"), S8("vaddps 4(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x50, 0x01}},
        {S8("VADDPS"), S8("vaddps -4(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x50, 0xff}},
        {S8("VADDPS"), S8("vaddps 16(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 16, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x50, 0x04}},
        {S8("VADDPS"), S8("vaddps -16(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -16, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x50, 0xfc}},
        {S8("VADDPS"), S8("vaddps 508(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 508, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x50, 0x7f}},
        {S8("VADDPS"), S8("vaddps -512(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -512, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x50, 0x80}},
        {S8("VADDPS"), S8("vaddps 512(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 512, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x90, 0x00, 0x02, 0x00, 0x00}},
        {S8("VADDPS"), S8("vaddps -516(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -516, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x90, 0xfc, 0xfd, 0xff, 0xff}},
        {S8("VADDPS"), S8("vaddps 1(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VADDPS"), S8("vaddps 509(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 509, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x18, 0x58, 0x90, 0xfd, 0x01, 0x00, 0x00}},
        {S8("VADDPD"), S8("vaddpd 0(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x10}},
        {S8("VADDPD"), S8("vaddpd 8(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x50, 0x01}},
        {S8("VADDPD"), S8("vaddpd -8(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x50, 0xff}},
        {S8("VADDPD"), S8("vaddpd 16(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 16, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x50, 0x02}},
        {S8("VADDPD"), S8("vaddpd -16(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -16, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x50, 0xfe}},
        {S8("VADDPD"), S8("vaddpd 1016(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1016, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x50, 0x7f}},
        {S8("VADDPD"), S8("vaddpd -1024(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -1024, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x50, 0x80}},
        {S8("VADDPD"), S8("vaddpd 1024(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1024, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x90, 0x00, 0x04, 0x00, 0x00}},
        {S8("VADDPD"), S8("vaddpd -1032(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -1032, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x90, 0xf8, 0xfb, 0xff, 0xff}},
        {S8("VADDPD"), S8("vaddpd 1(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VADDPD"), S8("vaddpd 1017(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1017, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x18, 0x58, 0x90, 0xf9, 0x03, 0x00, 0x00}},
        {S8("VPADDD"), S8("vpaddd 0(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x10}},
        {S8("VPADDD"), S8("vpaddd 4(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x50, 0x01}},
        {S8("VPADDD"), S8("vpaddd -4(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x50, 0xff}},
        {S8("VPADDD"), S8("vpaddd 16(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 16, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x50, 0x04}},
        {S8("VPADDD"), S8("vpaddd -16(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -16, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x50, 0xfc}},
        {S8("VPADDD"), S8("vpaddd 508(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 508, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x50, 0x7f}},
        {S8("VPADDD"), S8("vpaddd -512(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -512, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x50, 0x80}},
        {S8("VPADDD"), S8("vpaddd 512(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 512, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x90, 0x00, 0x02, 0x00, 0x00}},
        {S8("VPADDD"), S8("vpaddd -516(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -516, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x90, 0xfc, 0xfd, 0xff, 0xff}},
        {S8("VPADDD"), S8("vpaddd 1(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VPADDD"), S8("vpaddd 509(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 509, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x18, 0xfe, 0x90, 0xfd, 0x01, 0x00, 0x00}},
        {S8("VPADDQ"), S8("vpaddq 0(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x10}},
        {S8("VPADDQ"), S8("vpaddq 8(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x50, 0x01}},
        {S8("VPADDQ"), S8("vpaddq -8(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x50, 0xff}},
        {S8("VPADDQ"), S8("vpaddq 16(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 16, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x50, 0x02}},
        {S8("VPADDQ"), S8("vpaddq -16(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -16, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x50, 0xfe}},
        {S8("VPADDQ"), S8("vpaddq 1016(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1016, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x50, 0x7f}},
        {S8("VPADDQ"), S8("vpaddq -1024(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -1024, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x50, 0x80}},
        {S8("VPADDQ"), S8("vpaddq 1024(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1024, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x90, 0x00, 0x04, 0x00, 0x00}},
        {S8("VPADDQ"), S8("vpaddq -1032(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -1032, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x90, 0xf8, 0xfb, 0xff, 0xff}},
        {S8("VPADDQ"), S8("vpaddq 1(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VPADDQ"), S8("vpaddq 1017(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1017, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x18, 0xd4, 0x90, 0xf9, 0x03, 0x00, 0x00}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 0(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 0, 0, 255, 0, 64, 6, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x10}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 4(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 4, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x50, 0x01}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -4(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -4, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x50, 0xff}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 16(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 16, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x50, 0x04}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -16(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -16, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x50, 0xfc}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 508(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 508, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x50, 0x7f}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -512(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -512, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x50, 0x80}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 512(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 512, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x90, 0x00, 0x02, 0x00, 0x00}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -516(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, -516, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x90, 0xfc, 0xfd, 0xff, 0xff}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 1(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 1, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 509(%rax){1to4}, %xmm1, %xmm2\n"), 128, 32, 509, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x18, 0x98, 0x90, 0xfd, 0x01, 0x00, 0x00}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 0(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 0, 0, 255, 0, 64, 6, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x10}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 8(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 8, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x50, 0x01}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -8(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -8, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x50, 0xff}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 16(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 16, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x50, 0x02}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -16(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -16, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x50, 0xfe}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1016(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1016, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x50, 0x7f}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -1024(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -1024, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x50, 0x80}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1024(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1024, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x90, 0x00, 0x04, 0x00, 0x00}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -1032(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, -1032, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x90, 0xf8, 0xfb, 0xff, 0xff}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1017(%rax){1to2}, %xmm1, %xmm2\n"), 128, 64, 1017, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x18, 0x98, 0x90, 0xf9, 0x03, 0x00, 0x00}},
        {S8("VADDPS"), S8("vaddps 0(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x10}},
        {S8("VADDPS"), S8("vaddps 4(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x50, 0x01}},
        {S8("VADDPS"), S8("vaddps -4(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x50, 0xff}},
        {S8("VADDPS"), S8("vaddps 32(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 32, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x50, 0x08}},
        {S8("VADDPS"), S8("vaddps -32(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -32, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x50, 0xf8}},
        {S8("VADDPS"), S8("vaddps 508(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 508, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x50, 0x7f}},
        {S8("VADDPS"), S8("vaddps -512(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -512, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x50, 0x80}},
        {S8("VADDPS"), S8("vaddps 512(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 512, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x90, 0x00, 0x02, 0x00, 0x00}},
        {S8("VADDPS"), S8("vaddps -516(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -516, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x90, 0xfc, 0xfd, 0xff, 0xff}},
        {S8("VADDPS"), S8("vaddps 1(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VADDPS"), S8("vaddps 509(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 509, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x38, 0x58, 0x90, 0xfd, 0x01, 0x00, 0x00}},
        {S8("VADDPD"), S8("vaddpd 0(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x10}},
        {S8("VADDPD"), S8("vaddpd 8(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x50, 0x01}},
        {S8("VADDPD"), S8("vaddpd -8(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x50, 0xff}},
        {S8("VADDPD"), S8("vaddpd 32(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 32, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x50, 0x04}},
        {S8("VADDPD"), S8("vaddpd -32(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -32, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x50, 0xfc}},
        {S8("VADDPD"), S8("vaddpd 1016(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1016, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x50, 0x7f}},
        {S8("VADDPD"), S8("vaddpd -1024(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -1024, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x50, 0x80}},
        {S8("VADDPD"), S8("vaddpd 1024(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1024, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x90, 0x00, 0x04, 0x00, 0x00}},
        {S8("VADDPD"), S8("vaddpd -1032(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -1032, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x90, 0xf8, 0xfb, 0xff, 0xff}},
        {S8("VADDPD"), S8("vaddpd 1(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VADDPD"), S8("vaddpd 1017(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1017, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x38, 0x58, 0x90, 0xf9, 0x03, 0x00, 0x00}},
        {S8("VPADDD"), S8("vpaddd 0(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x10}},
        {S8("VPADDD"), S8("vpaddd 4(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x50, 0x01}},
        {S8("VPADDD"), S8("vpaddd -4(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x50, 0xff}},
        {S8("VPADDD"), S8("vpaddd 32(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 32, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x50, 0x08}},
        {S8("VPADDD"), S8("vpaddd -32(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -32, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x50, 0xf8}},
        {S8("VPADDD"), S8("vpaddd 508(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 508, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x50, 0x7f}},
        {S8("VPADDD"), S8("vpaddd -512(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -512, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x50, 0x80}},
        {S8("VPADDD"), S8("vpaddd 512(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 512, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x90, 0x00, 0x02, 0x00, 0x00}},
        {S8("VPADDD"), S8("vpaddd -516(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -516, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x90, 0xfc, 0xfd, 0xff, 0xff}},
        {S8("VPADDD"), S8("vpaddd 1(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VPADDD"), S8("vpaddd 509(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 509, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x38, 0xfe, 0x90, 0xfd, 0x01, 0x00, 0x00}},
        {S8("VPADDQ"), S8("vpaddq 0(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x10}},
        {S8("VPADDQ"), S8("vpaddq 8(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x50, 0x01}},
        {S8("VPADDQ"), S8("vpaddq -8(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x50, 0xff}},
        {S8("VPADDQ"), S8("vpaddq 32(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 32, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x50, 0x04}},
        {S8("VPADDQ"), S8("vpaddq -32(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -32, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x50, 0xfc}},
        {S8("VPADDQ"), S8("vpaddq 1016(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1016, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x50, 0x7f}},
        {S8("VPADDQ"), S8("vpaddq -1024(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -1024, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x50, 0x80}},
        {S8("VPADDQ"), S8("vpaddq 1024(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1024, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x90, 0x00, 0x04, 0x00, 0x00}},
        {S8("VPADDQ"), S8("vpaddq -1032(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -1032, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x90, 0xf8, 0xfb, 0xff, 0xff}},
        {S8("VPADDQ"), S8("vpaddq 1(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VPADDQ"), S8("vpaddq 1017(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1017, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x38, 0xd4, 0x90, 0xf9, 0x03, 0x00, 0x00}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 0(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 0, 0, 255, 0, 64, 6, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x10}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 4(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 4, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x50, 0x01}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -4(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -4, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x50, 0xff}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 32(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 32, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x50, 0x08}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -32(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -32, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x50, 0xf8}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 508(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 508, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x50, 0x7f}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -512(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -512, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x50, 0x80}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 512(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 512, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x90, 0x00, 0x02, 0x00, 0x00}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -516(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, -516, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x90, 0xfc, 0xfd, 0xff, 0xff}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 1(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 1, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 509(%rax){1to8}, %ymm1, %ymm2\n"), 256, 32, 509, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x38, 0x98, 0x90, 0xfd, 0x01, 0x00, 0x00}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 0(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 0, 0, 255, 0, 64, 6, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x10}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 8(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 8, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x50, 0x01}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -8(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -8, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x50, 0xff}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 32(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 32, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x50, 0x04}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -32(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -32, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x50, 0xfc}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1016(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1016, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x50, 0x7f}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -1024(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -1024, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x50, 0x80}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1024(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1024, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x90, 0x00, 0x04, 0x00, 0x00}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -1032(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, -1032, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x90, 0xf8, 0xfb, 0xff, 0xff}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1017(%rax){1to4}, %ymm1, %ymm2\n"), 256, 64, 1017, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x38, 0x98, 0x90, 0xf9, 0x03, 0x00, 0x00}},
        {S8("VADDPS"), S8("vaddps 0(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x10}},
        {S8("VADDPS"), S8("vaddps 4(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x50, 0x01}},
        {S8("VADDPS"), S8("vaddps -4(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x50, 0xff}},
        {S8("VADDPS"), S8("vaddps 64(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 64, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x50, 0x10}},
        {S8("VADDPS"), S8("vaddps -64(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -64, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x50, 0xf0}},
        {S8("VADDPS"), S8("vaddps 508(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 508, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x50, 0x7f}},
        {S8("VADDPS"), S8("vaddps -512(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -512, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x50, 0x80}},
        {S8("VADDPS"), S8("vaddps 512(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 512, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x90, 0x00, 0x02, 0x00, 0x00}},
        {S8("VADDPS"), S8("vaddps -516(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -516, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x90, 0xfc, 0xfd, 0xff, 0xff}},
        {S8("VADDPS"), S8("vaddps 1(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VADDPS"), S8("vaddps 509(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 509, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x90, 0xfd, 0x01, 0x00, 0x00}},
        {S8("VADDPD"), S8("vaddpd 0(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x10}},
        {S8("VADDPD"), S8("vaddpd 8(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x50, 0x01}},
        {S8("VADDPD"), S8("vaddpd -8(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x50, 0xff}},
        {S8("VADDPD"), S8("vaddpd 64(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 64, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x50, 0x08}},
        {S8("VADDPD"), S8("vaddpd -64(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -64, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x50, 0xf8}},
        {S8("VADDPD"), S8("vaddpd 1016(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1016, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x50, 0x7f}},
        {S8("VADDPD"), S8("vaddpd -1024(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -1024, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x50, 0x80}},
        {S8("VADDPD"), S8("vaddpd 1024(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1024, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x90, 0x00, 0x04, 0x00, 0x00}},
        {S8("VADDPD"), S8("vaddpd -1032(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -1032, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x90, 0xf8, 0xfb, 0xff, 0xff}},
        {S8("VADDPD"), S8("vaddpd 1(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VADDPD"), S8("vaddpd 1017(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1017, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x58, 0x58, 0x90, 0xf9, 0x03, 0x00, 0x00}},
        {S8("VPADDD"), S8("vpaddd 0(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x10}},
        {S8("VPADDD"), S8("vpaddd 4(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x50, 0x01}},
        {S8("VPADDD"), S8("vpaddd -4(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -4, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x50, 0xff}},
        {S8("VPADDD"), S8("vpaddd 64(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 64, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x50, 0x10}},
        {S8("VPADDD"), S8("vpaddd -64(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -64, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x50, 0xf0}},
        {S8("VPADDD"), S8("vpaddd 508(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 508, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x50, 0x7f}},
        {S8("VPADDD"), S8("vpaddd -512(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -512, 0, 255, 0, 64, 7, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x50, 0x80}},
        {S8("VPADDD"), S8("vpaddd 512(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 512, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x90, 0x00, 0x02, 0x00, 0x00}},
        {S8("VPADDD"), S8("vpaddd -516(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -516, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x90, 0xfc, 0xfd, 0xff, 0xff}},
        {S8("VPADDD"), S8("vpaddd 1(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VPADDD"), S8("vpaddd 509(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 509, 0, 255, 0, 64, 10, {0x62, 0xf1, 0x75, 0x58, 0xfe, 0x90, 0xfd, 0x01, 0x00, 0x00}},
        {S8("VPADDQ"), S8("vpaddq 0(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 0, 0, 255, 0, 64, 6, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x10}},
        {S8("VPADDQ"), S8("vpaddq 8(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x50, 0x01}},
        {S8("VPADDQ"), S8("vpaddq -8(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -8, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x50, 0xff}},
        {S8("VPADDQ"), S8("vpaddq 64(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 64, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x50, 0x08}},
        {S8("VPADDQ"), S8("vpaddq -64(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -64, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x50, 0xf8}},
        {S8("VPADDQ"), S8("vpaddq 1016(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1016, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x50, 0x7f}},
        {S8("VPADDQ"), S8("vpaddq -1024(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -1024, 0, 255, 0, 64, 7, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x50, 0x80}},
        {S8("VPADDQ"), S8("vpaddq 1024(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1024, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x90, 0x00, 0x04, 0x00, 0x00}},
        {S8("VPADDQ"), S8("vpaddq -1032(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -1032, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x90, 0xf8, 0xfb, 0xff, 0xff}},
        {S8("VPADDQ"), S8("vpaddq 1(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VPADDQ"), S8("vpaddq 1017(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1017, 0, 255, 0, 64, 10, {0x62, 0xf1, 0xf5, 0x58, 0xd4, 0x90, 0xf9, 0x03, 0x00, 0x00}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 0(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 0, 0, 255, 0, 64, 6, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x10}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 4(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 4, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x50, 0x01}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -4(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -4, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x50, 0xff}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 64(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 64, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x50, 0x10}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -64(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -64, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x50, 0xf0}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 508(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 508, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x50, 0x7f}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -512(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -512, 0, 255, 0, 64, 7, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x50, 0x80}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 512(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 512, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x90, 0x00, 0x02, 0x00, 0x00}},
        {S8("VFMADD132PS"), S8("vfmadd132ps -516(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, -516, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x90, 0xfc, 0xfd, 0xff, 0xff}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 1(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 1, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VFMADD132PS"), S8("vfmadd132ps 509(%rax){1to16}, %zmm1, %zmm2\n"), 512, 32, 509, 0, 255, 0, 64, 10, {0x62, 0xf2, 0x75, 0x58, 0x98, 0x90, 0xfd, 0x01, 0x00, 0x00}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 0(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 0, 0, 255, 0, 64, 6, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x10}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 8(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 8, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x50, 0x01}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -8(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -8, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x50, 0xff}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 64(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 64, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x50, 0x08}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -64(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -64, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x50, 0xf8}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1016(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1016, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x50, 0x7f}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -1024(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -1024, 0, 255, 0, 64, 7, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x50, 0x80}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1024(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1024, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x90, 0x00, 0x04, 0x00, 0x00}},
        {S8("VFMADD132PD"), S8("vfmadd132pd -1032(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, -1032, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x90, 0xf8, 0xfb, 0xff, 0xff}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x90, 0x01, 0x00, 0x00, 0x00}},
        {S8("VFMADD132PD"), S8("vfmadd132pd 1017(%rax){1to8}, %zmm1, %zmm2\n"), 512, 64, 1017, 0, 255, 0, 64, 10, {0x62, 0xf2, 0xf5, 0x58, 0x98, 0x90, 0xf9, 0x03, 0x00, 0x00}},
        {S8("VADDPS"), S8("vaddps 64(%rbp){1to16}, %zmm1, %zmm2\n"), 512, 32, 64, 5, 255, 0, 64, 7, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x55, 0x10}},
        {S8("VADDPS"), S8("vaddps 64(%r13){1to16}, %zmm1, %zmm2\n"), 512, 32, 64, 13, 255, 0, 64, 7, {0x62, 0xd1, 0x74, 0x58, 0x58, 0x55, 0x10}},
        {S8("VADDPS"), S8("vaddps 64(%rsp){1to16}, %zmm1, %zmm2\n"), 512, 32, 64, 4, 255, 0, 64, 8, {0x62, 0xf1, 0x74, 0x58, 0x58, 0x54, 0x24, 0x10}},
        {S8("VADDPS"), S8("vaddps 64(%r8,%r9,4){1to16}, %zmm1, %zmm2\n"), 512, 32, 64, 8, 9, 4, 64, 8, {0x62, 0x91, 0x74, 0x58, 0x58, 0x54, 0x88, 0x10}},
        {S8("VADDPS"), S8("vaddps 64(%eax,%ecx,2){1to16}, %zmm1, %zmm2\n"), 512, 32, 64, 0, 1, 2, 32, 9, {0x67, 0x62, 0xf1, 0x74, 0x58, 0x58, 0x54, 0x48, 0x10}},
    };
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX,
                     .cpu_model = CPU_MODEL_INTEL_DIAMOND_RAPIDS};
    String8 features[] = {S8("avx512f"), S8("avx512vl")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        struct BroadcastCase* fixture = cases + index;
        u8 reg_class = fixture->vector_bits == 128 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM
                         : fixture->vector_bits == 256 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM
                                                       : BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM;
        BusterX86MetadataPhysicalOperand memory = x86_64_metadata_test_physical_mem_base(
            fixture->base, fixture->element_bits, fixture->displacement);
        memory.memory.address_size = fixture->address_size;
        memory.memory.base.width = fixture->address_size;
        if (fixture->index != UINT8_MAX)
        {
            memory.memory.has_index = true;
            memory.memory.index = (BusterX86MetadataPhysicalRegister){
                .index = fixture->index, .width = fixture->address_size,
                .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
            };
            memory.memory.scale = fixture->scale;
        }
        BusterX86MetadataPhysicalOperand operands[] = {
            x86_64_metadata_test_physical_reg(reg_class, 2, fixture->vector_bits),
            x86_64_metadata_test_physical_reg(reg_class, 1, fixture->vector_bits), memory,
        };
        BusterX86MetadataPhysicalAttributes attributes = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_BROADCAST,
            .broadcast_elements = (u8)(fixture->vector_bits / fixture->element_bits),
        };
        BusterX86MetadataPhysicalQuery physical = x86_64_metadata_test_physical_query(
            fixture->mnemonic, operands, BUSTER_ARRAY_LENGTH(operands), attributes,
            features, BUSTER_ARRAY_LENGTH(features));
        physical.address_size = fixture->address_size;
        BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(physical);
        BusterX86MetadataFormKey key = {0};
        BusterX86MetadataExactPlan plan = {0};
        bool ready = selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                     buster_x86_metadata_form_key(selected.form_id, &key) &&
                     buster_x86_metadata_exact_plan_for_key(key, &plan);
        BUSTER_TEST(arguments, ready);
        if (!ready)
        {
            arguments->show(arguments, S8("X86_BROADCAST_PREPARE case={u32} form={u32} select_status={u32}\n"),
                            index, selected.form_id, selected.status);
        }
        if (ready)
        {
            u8 bytes[16] = {0};
            BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = physical, .form_id = selected.form_id,
                .output = bytes, .output_capacity = BUSTER_ARRAY_LENGTH(bytes),
            });
            bool direct = emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && !emitted.relocation_count &&
                          x86_64_metadata_test_bytes_equal(bytes, emitted.byte_count, fixture->bytes, fixture->byte_count);
            BUSTER_TEST(arguments, direct);
            BusterX86MetadataExactQuery exact = {
                .key = key, .operands = operands, .operand_count = BUSTER_ARRAY_LENGTH(operands),
                .features = {.names = features, .count = BUSTER_ARRAY_LENGTH(features)},
                .attributes = attributes, .address_size = fixture->address_size,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
                .output = bytes, .output_capacity = BUSTER_ARRAY_LENGTH(bytes),
            };
            emitted = buster_x86_metadata_emit_exact_query(exact);
            bool checked = emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && !emitted.relocation_count &&
                           x86_64_metadata_test_bytes_equal(bytes, emitted.byte_count, fixture->bytes, fixture->byte_count);
            BUSTER_TEST(arguments, checked);
            emitted = buster_x86_metadata_emit_exact_prevalidated(plan, exact);
            bool prepared = emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && !emitted.relocation_count &&
                            x86_64_metadata_test_bytes_equal(bytes, emitted.byte_count, fixture->bytes, fixture->byte_count);
            BUSTER_TEST(arguments, prepared);
            AssemblyEncodeResult encoded = assembly_encode(arguments->arena, fixture->source,
                (AssemblyEncodeOptions){.target = target, .syntax = ASSEMBLY_SYNTAX_ATT});
            bool source = !encoded.diagnostic_count && !encoded.relocation_count &&
                          x86_64_metadata_test_bytes_equal(encoded.bytes.pointer, (u32)encoded.bytes.length,
                                                           fixture->bytes, fixture->byte_count);
            BUSTER_TEST(arguments, source);
            if (!direct || !checked || !prepared || !source)
            {
                arguments->show(arguments, S8("X86_BROADCAST_REGRESSION case={u32} form={u32} direct={u32} checked={u32} prepared={u32} source={u32} input={S8}"),
                                index, selected.form_id, direct, checked, prepared, source, fixture->source);
            }
        }
    }
    return result;
}
