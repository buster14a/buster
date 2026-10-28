// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Eight bounded register spellings at a time. The two short-name tables fit in registers; indexed memory gathers
// are deliberately absent. The full scalar spelling grammar stays independent in asm_register.

#define BUSTER_ASM_REGISTER_CACHED (1u << 31)

static __m512i asm_word_registers_vector(__m512i words, __m512i sizes)
{
    // Fold only ASCII uppercase letters. Padding, numbers, punctuation and non-ASCII bytes retain their value.
    const __mmask64 uppercase = _mm512_cmple_epu8_mask(_mm512_sub_epi8(words, _mm512_set1_epi8('A')), _mm512_set1_epi8('Z' - 'A'));
    words = _mm512_mask_add_epi8(words, uppercase, words, _mm512_set1_epi8('a' - 'A'));
    // (first + 3 * second) & 31 is collision-free for these sixteen two-character roots. Compare the full key after
    // the permute: an unknown name with the same hash must never acquire a register's meaning.
    static const uint16_t keys[32] __attribute__((aligned(64))) =
    {
        [3] = 's' | 'p' << 8,
        [5] = 'a' | 'l' << 8,
        [6] = 'b' | 'l' << 8,
        [7] = 'c' | 'l' << 8,
        [8] = 'd' | 'l' << 8,
        [9] = 'a' | 'x' << 8,
        [10] = 'b' | 'x' << 8,
        [11] = 'c' | 'x' << 8,
        [12] = 'd' | 'x' << 8,
        [14] = 's' | 'i' << 8,
        [18] = 'b' | 'p' << 8,
        [25] = 'a' | 'h' << 8,
        [26] = 'b' | 'h' << 8,
        [27] = 'c' | 'h' << 8,
        [28] = 'd' | 'h' << 8,
        [31] = 'd' | 'i' << 8,
    };
    static const uint16_t properties[32] __attribute__((aligned(64))) =
    {
        [3] = 4 | BUSTER_X86_CLASS_GPR16 << 8,
        [5] = 0 | BUSTER_X86_CLASS_GPR8 << 8,
        [6] = 3 | BUSTER_X86_CLASS_GPR8 << 8,
        [7] = 1 | BUSTER_X86_CLASS_GPR8 << 8,
        [8] = 2 | BUSTER_X86_CLASS_GPR8 << 8,
        [9] = 0 | BUSTER_X86_CLASS_GPR16 << 8,
        [10] = 3 | BUSTER_X86_CLASS_GPR16 << 8,
        [11] = 1 | BUSTER_X86_CLASS_GPR16 << 8,
        [12] = 2 | BUSTER_X86_CLASS_GPR16 << 8,
        [14] = 6 | BUSTER_X86_CLASS_GPR16 << 8,
        [18] = 5 | BUSTER_X86_CLASS_GPR16 << 8,
        [25] = X86_REGISTER_AH | BUSTER_X86_CLASS_GPR8 << 8,
        [26] = X86_REGISTER_BH | BUSTER_X86_CLASS_GPR8 << 8,
        [27] = X86_REGISTER_CH | BUSTER_X86_CLASS_GPR8 << 8,
        [28] = X86_REGISTER_DH | BUSTER_X86_CLASS_GPR8 << 8,
        [31] = 7 | BUSTER_X86_CLASS_GPR16 << 8,
    };
    const __m512i byte_mask = _mm512_set1_epi64(255);
    const __m512i first = _mm512_and_si512(words, byte_mask);
    const __mmask8 three = _mm512_cmpeq_epi64_mask(sizes, _mm512_set1_epi64(3));
    const __mmask8 rex = _mm512_cmpeq_epi64_mask(first, _mm512_set1_epi64('r'));
    const __mmask8 eax = _mm512_cmpeq_epi64_mask(first, _mm512_set1_epi64('e'));
    const __mmask8 prefix = three & (rex | eax);
    const __m512i roots = _mm512_mask_srli_epi64(words, prefix, words, 8);
    const __m512i second = _mm512_and_si512(_mm512_srli_epi64(roots, 8), byte_mask);
    const __m512i hash = _mm512_add_epi64(_mm512_and_si512(roots, byte_mask), _mm512_add_epi64(second, _mm512_slli_epi64(second, 1)));
    const __m512i expected = _mm512_and_si512(_mm512_permutexvar_epi16(hash, _mm512_load_si512(keys)), _mm512_set1_epi64(65535));
    const __m512i properties_found = _mm512_and_si512(_mm512_permutexvar_epi16(hash, _mm512_load_si512(properties)), _mm512_set1_epi64(65535));
    const __m512i reg = _mm512_and_si512(properties_found, byte_mask);
    const __m512i root_class = _mm512_srli_epi64(properties_found, 8);
    const __mmask8 word = _mm512_cmpeq_epi64_mask(root_class, _mm512_set1_epi64(BUSTER_X86_CLASS_GPR16));
    const __mmask8 suffix = three & ~prefix & word & _mm512_cmpge_epi64_mask(reg, _mm512_set1_epi64(4)) & _mm512_cmpeq_epi64_mask(_mm512_srli_epi64(words, 16), _mm512_set1_epi64('l'));
    __mmask8 short_name = _mm512_cmpeq_epi64_mask(_mm512_and_si512(roots, _mm512_set1_epi64(65535)), expected);
    short_name &= _mm512_test_epi64_mask(root_class, root_class);
    short_name &= _mm512_cmpeq_epi64_mask(sizes, _mm512_set1_epi64(2)) | (prefix & word) | suffix;
    __m512i class = _mm512_mask_mov_epi64(root_class, prefix & eax, _mm512_set1_epi64(BUSTER_X86_CLASS_GPR32));
    class = _mm512_mask_mov_epi64(class, prefix & rex, _mm512_set1_epi64(BUSTER_X86_CLASS_GPR64));
    class = _mm512_mask_mov_epi64(class, suffix, _mm512_set1_epi64(BUSTER_X86_CLASS_GPR8));
    const __m512i width = _mm512_sllv_epi64(_mm512_set1_epi64(1), _mm512_sub_epi64(class, _mm512_set1_epi64(1)));
    __m512i descriptions = _mm512_or_si512(_mm512_set1_epi64(X86_OPERAND_REGISTER), _mm512_or_si512(_mm512_slli_epi64(reg, 8), _mm512_or_si512(_mm512_slli_epi64(class, 16), _mm512_slli_epi64(width, 24))));
    descriptions = _mm512_maskz_mov_epi64(short_name, descriptions);

    const __mmask8 vector_name = _mm512_cmpeq_epi64_mask(_mm512_and_si512(_mm512_srli_epi64(words, 8), _mm512_set1_epi64(65535)), _mm512_set1_epi64('m' | 'm' << 8)) & _mm512_cmpge_epu64_mask(first, _mm512_set1_epi64('x')) & _mm512_cmple_epu64_mask(first, _mm512_set1_epi64('z'));
    const __m512i next = _mm512_sub_epi64(_mm512_and_si512(_mm512_srli_epi64(words, 8), byte_mask), _mm512_set1_epi64('0'));
    const __mmask8 numbered_gpr = rex & _mm512_cmple_epu64_mask(next, _mm512_set1_epi64(9));
    const __mmask8 opmask = _mm512_cmpeq_epi64_mask(first, _mm512_set1_epi64('k'));
    const __mmask8 numbered = numbered_gpr | vector_name | opmask;
    const __m512i last_shift = _mm512_slli_epi64(_mm512_sub_epi64(sizes, _mm512_set1_epi64(1)), 3);
    const __m512i last = _mm512_and_si512(_mm512_srlv_epi64(words, last_shift), byte_mask);
    const __mmask8 byte_suffix = numbered_gpr & _mm512_cmpeq_epi64_mask(last, _mm512_set1_epi64('b'));
    const __mmask8 word_suffix = numbered_gpr & _mm512_cmpeq_epi64_mask(last, _mm512_set1_epi64('w'));
    const __mmask8 dword_suffix = numbered_gpr & _mm512_cmpeq_epi64_mask(last, _mm512_set1_epi64('d'));
    const __mmask8 sized = byte_suffix | word_suffix | dword_suffix;
    const __m512i start = _mm512_mask_mov_epi64(_mm512_set1_epi64(1), vector_name, _mm512_set1_epi64(3));
    const __m512i digits = _mm512_sub_epi64(_mm512_sub_epi64(sizes, start), _mm512_maskz_set1_epi64(sized, 1));
    const __m512i text = _mm512_srlv_epi64(words, _mm512_slli_epi64(start, 3));
    const __m512i first_digit = _mm512_sub_epi64(_mm512_and_si512(text, byte_mask), _mm512_set1_epi64('0'));
    const __m512i second_digit = _mm512_sub_epi64(_mm512_and_si512(_mm512_srli_epi64(text, 8), byte_mask), _mm512_set1_epi64('0'));
    const __mmask8 one = _mm512_cmpeq_epi64_mask(digits, _mm512_set1_epi64(1));
    const __mmask8 two = _mm512_cmpeq_epi64_mask(digits, _mm512_set1_epi64(2));
    __mmask8 number_valid = numbered & (one | two) & _mm512_cmple_epu64_mask(first_digit, _mm512_set1_epi64(9));
    number_valid &= one | _mm512_cmple_epu64_mask(second_digit, _mm512_set1_epi64(9));
    number_valid &= one | _mm512_cmpneq_epi64_mask(first_digit, _mm512_setzero_si512());
    const __m512i number = _mm512_mask_mov_epi64(first_digit, two, _mm512_add_epi64(_mm512_mullo_epi64(first_digit, _mm512_set1_epi64(10)), second_digit));
    number_valid &= (~numbered_gpr | _mm512_cmpge_epi64_mask(number, _mm512_set1_epi64(8)));
    const __m512i limit = _mm512_mask_mov_epi64(_mm512_mask_mov_epi64(_mm512_set1_epi64(16), vector_name, _mm512_set1_epi64(32)), opmask, _mm512_set1_epi64(8));
    number_valid &= _mm512_cmplt_epu64_mask(number, limit);
    class = _mm512_set1_epi64(BUSTER_X86_CLASS_GPR64);
    class = _mm512_mask_mov_epi64(class, byte_suffix, _mm512_set1_epi64(BUSTER_X86_CLASS_GPR8));
    class = _mm512_mask_mov_epi64(class, word_suffix, _mm512_set1_epi64(BUSTER_X86_CLASS_GPR16));
    class = _mm512_mask_mov_epi64(class, dword_suffix, _mm512_set1_epi64(BUSTER_X86_CLASS_GPR32));
    __m512i number_width = _mm512_sllv_epi64(_mm512_set1_epi64(1), _mm512_sub_epi64(class, _mm512_set1_epi64(1)));
    const __m512i vector_kind = _mm512_sub_epi64(first, _mm512_set1_epi64('x'));
    class = _mm512_mask_mov_epi64(class, vector_name, _mm512_add_epi64(vector_kind, _mm512_set1_epi64(BUSTER_X86_CLASS_XMM)));
    number_width = _mm512_mask_mov_epi64(number_width, vector_name, _mm512_sllv_epi64(_mm512_set1_epi64(16), vector_kind));
    class = _mm512_mask_mov_epi64(class, opmask, _mm512_set1_epi64(BUSTER_X86_CLASS_OPMASK));
    number_width = _mm512_mask_mov_epi64(number_width, opmask, _mm512_setzero_si512());
    const __m512i numbered_description = _mm512_or_si512(_mm512_set1_epi64(X86_OPERAND_REGISTER), _mm512_or_si512(_mm512_slli_epi64(number, 8), _mm512_or_si512(_mm512_slli_epi64(class, 16), _mm512_slli_epi64(number_width, 24))));
    descriptions = _mm512_mask_mov_epi64(descriptions, number_valid, numbered_description);
    const __mmask8 rip = three & _mm512_cmpeq_epi64_mask(words, _mm512_set1_epi64('r' | 'i' << 8 | 'p' << 16));
    descriptions = _mm512_mask_mov_epi64(descriptions, rip, _mm512_set1_epi64(X86_OPERAND_REGISTER | 255u << 16 | 8u << 24));
    // Register numbers have canonical spelling: leading-zero names such as r08 and xmm00 are symbols.
    return _mm512_or_si512(descriptions, _mm512_set1_epi64(BUSTER_ASM_REGISTER_CACHED));
}

static void asm_cache_registers(AsmParser* parser)
{
#if !BUSTER_HOST_VECTOR
    (void)parser; // The independent asm_register grammar handles each word.
#else
    uint64_t keys[64];
    uint64_t lengths[64];
    uint64_t descriptions[64];
    uint32_t tokens[64];
    uint32_t at = 0;
    while (at < parser->token_count)
    {
        uint32_t count = 0;
        for (; at < parser->token_count && count < 64; at += 1)
        {
            const AsmToken token = parser->tokens[at];
            const uint32_t length = token.end - token.start;
            if (token.kind == BUSTER_ASM_WORD && length <= 8)
            {
                keys[count] = 0;
                memcpy(keys + count, parser->source.pointer + token.start, length);
                lengths[count] = length;
                tokens[count++] = at;
            }
        }
        for (uint32_t first = 0; first < count; first += 8)
        {
            const uint32_t left = count - first;
            const __mmask8 valid = left >= 8 ? 255 : (1u << left) - 1;
            const __m512i words = _mm512_maskz_loadu_epi64(valid, keys + first);
            const __m512i sizes = _mm512_maskz_loadu_epi64(valid, lengths + first);
            _mm512_mask_storeu_epi64(descriptions + first, valid, asm_word_registers_vector(words, sizes));
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            parser->tokens[tokens[i]].reserved = descriptions[i];
        }
    }
#endif
}
