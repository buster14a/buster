# Original instruction witnesses for the ordinary Jcc/SETcc/CMOVcc projection.
# Authored for Buster; no instruction descriptions or external fixtures copied.
# Assemble with GNU as --64 or clang -c -target x86_64-linux-gnu and inspect
# .text with objdump -dr. The first 30 branches target the following instruction,
# so their independently produced short displacements must be zero. The 30
# spellings in each group deliberately include all architectural aliases.
# No encoded byte directives: assembler output is independent of Buster's
# condition table and of its generated XED encoding snapshot.

.intel_syntax noprefix
.text
.globl buster_condition_oracle
buster_condition_oracle:
    jo .Ljo
.Ljo:
    jno .Ljno
.Ljno:
    jb .Ljb
.Ljb:
    jc .Ljc
.Ljc:
    jnae .Ljnae
.Ljnae:
    jae .Ljae
.Ljae:
    jnb .Ljnb
.Ljnb:
    jnc .Ljnc
.Ljnc:
    je .Lje
.Lje:
    jz .Ljz
.Ljz:
    jne .Ljne
.Ljne:
    jnz .Ljnz
.Ljnz:
    jbe .Ljbe
.Ljbe:
    jna .Ljna
.Ljna:
    ja .Lja
.Lja:
    jnbe .Ljnbe
.Ljnbe:
    js .Ljs
.Ljs:
    jns .Ljns
.Ljns:
    jp .Ljp
.Ljp:
    jpe .Ljpe
.Ljpe:
    jnp .Ljnp
.Ljnp:
    jpo .Ljpo
.Ljpo:
    jl .Ljl
.Ljl:
    jnge .Ljnge
.Ljnge:
    jge .Ljge
.Ljge:
    jnl .Ljnl
.Ljnl:
    jle .Ljle
.Ljle:
    jng .Ljng
.Ljng:
    jg .Ljg
.Ljg:
    jnle .Ljnle
.Ljnle:
    seto bl
    setno bl
    setb bl
    setc bl
    setnae bl
    setae bl
    setnb bl
    setnc bl
    sete bl
    setz bl
    setne bl
    setnz bl
    setbe bl
    setna bl
    seta bl
    setnbe bl
    sets bl
    setns bl
    setp bl
    setpe bl
    setnp bl
    setpo bl
    setl bl
    setnge bl
    setge bl
    setnl bl
    setle bl
    setng bl
    setg bl
    setnle bl
    cmovo eax, ecx
    cmovno eax, ecx
    cmovb eax, ecx
    cmovc eax, ecx
    cmovnae eax, ecx
    cmovae eax, ecx
    cmovnb eax, ecx
    cmovnc eax, ecx
    cmove eax, ecx
    cmovz eax, ecx
    cmovne eax, ecx
    cmovnz eax, ecx
    cmovbe eax, ecx
    cmovna eax, ecx
    cmova eax, ecx
    cmovnbe eax, ecx
    cmovs eax, ecx
    cmovns eax, ecx
    cmovp eax, ecx
    cmovpe eax, ecx
    cmovnp eax, ecx
    cmovpo eax, ecx
    cmovl eax, ecx
    cmovnge eax, ecx
    cmovge eax, ecx
    cmovnl eax, ecx
    cmovle eax, ecx
    cmovng eax, ecx
    cmovg eax, ecx
    cmovnle eax, ecx

# Independent operand-width, high-byte and extended-register samples.
    cmovge ax, cx
    cmovge rax, rcx
    setne ah
    setne r8b
    setne byte ptr [rax]
    cmove eax, dword ptr [rbp]
.Lhinted:
    cs je .Lhinted

# External targets require near relocations; the object retains PC32 fields.
    je buster_condition_oracle_external
    jne buster_condition_oracle_external

.att_syntax prefix
    cmoveq %rcx, %rax
    cmovgeq %rcx, %rax
    cmovgew %cx, %ax
