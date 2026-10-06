# Independent GNU as / LLVM EVEX source-layout oracle; encoding checks only.
.section .text.intel,"ax",@progbits
.intel_syntax noprefix
vmovdqu32 zmm31, zmmword ptr [r13]
vmovups zmm16, zmmword ptr [r12+r9*4+8128]
vmovups zmm16, zmmword ptr [r12+r9*4+8192]
vaddps zmm17{k3}{z}, zmm18, zmmword ptr [r13+external_evex]
vaddpd zmm31{k7}, zmm30, zmmword ptr [rip+external_packed]
vmulps zmm19, zmm20, dword ptr [r13+508]{1to16}
vmulps zmm19, zmm20, dword ptr [r13+512]{1to16}
vcmpps k5, zmm21, zmm22, 7
vpcmpb k3, zmm23, zmm24, 7
vrndscaleps zmm25, zmmword ptr [r13+external_round], 255
vaddps zmm27, zmm28, zmm29, {rn-sae}
vaddps xmm16{k2}, xmm17, xmmword ptr [r13+2032]
vaddps ymm18{k4}, ymm19, ymmword ptr [r13+4096]
.section .text.att,"ax",@progbits
.att_syntax prefix
vmovdqu32 (%r13), %zmm31
vmovups 8128(%r12,%r9,4), %zmm16
vmovups 8192(%r12,%r9,4), %zmm16
vaddps external_evex(%r13), %zmm18, %zmm17{%k3}{z}
vaddpd external_packed(%rip), %zmm30, %zmm31{%k7}
vmulps 508(%r13){1to16}, %zmm20, %zmm19
vmulps 512(%r13){1to16}, %zmm20, %zmm19
vcmpps $7, %zmm22, %zmm21, %k5
vpcmpb $7, %zmm24, %zmm23, %k3
vrndscaleps $255, external_round(%r13), %zmm25
vaddps {rn-sae}, %zmm29, %zmm28, %zmm27
vaddps 2032(%r13), %xmm17, %xmm16{%k2}
vaddps 4096(%r13), %ymm19, %ymm18{%k4}
