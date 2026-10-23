// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original target-bit arithmetic for the source reader's opt-in GNU constant-folding policy. Include after
// CheckIntegerFold and check_action_mask. The strict folder supplies its observed fault; this helper does not
// change that default or erase faults itself. The returned fault is the required extension/diagnostic witness.
// Clang's rules were read, not copied, from clang/lib/AST/ExprConstant.cpp at llvmorg-23.1.1 and llvmorg-23.1.3:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/AST/ExprConstant.cpp
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/clang/lib/AST/ExprConstant.cpp
// Original source fixtures check these rules against the installed Clang. No host signed arithmetic or shift
// by the host width occurs, including when a negative 128-bit shift count is the minimum representable value.
#ifndef BUSTER_C_CHECK_INTEGER_EXTENSION_INCLUDED
#define BUSTER_C_CHECK_INTEGER_EXTENSION_INCLUDED

typedef struct CheckIntegerExtension CheckIntegerExtension;
struct CheckIntegerExtension
{
    unsigned __int128 integer;
    CheckFault fault;
    bool applied;
};

static CheckIntegerExtension check_integer_extension(CheckIntegerFold fold, CheckFault fault)
{
    CheckIntegerExtension result = { 0 };
    const bool widths = fold.width != 0 && fold.width <= 128 && fold.right_width != 0 && fold.right_width <= 128;
    if (fold.active && widths && (fault == CHECK_FAULT_SIGNED_OVERFLOW || fault == CHECK_FAULT_SHIFT_COUNT || fault == CHECK_FAULT_SHIFT_VALUE))
    {
        const unsigned __int128 mask = check_action_mask(fold.width);
        const unsigned __int128 right_mask = check_action_mask(fold.right_width);
        const unsigned __int128 sign = (unsigned __int128)1 << (fold.width - 1);
        const unsigned __int128 left = fold.left & mask;
        const unsigned __int128 right = fold.right & right_mask;
        if (fault == CHECK_FAULT_SIGNED_OVERFLOW && fold.is_signed)
        {
            result.applied = true;
            switch (fold.op)
            {
                case CHECK_OP_NEGATIVE: result.integer = 0 - left; break;
                case CHECK_OP_ADD: result.integer = left + right; break;
                case CHECK_OP_SUBTRACT: result.integer = left - right; break;
                case CHECK_OP_MULTIPLY: result.integer = left * right; break;
                case CHECK_OP_DIVIDE:
                case CHECK_OP_REMAINDER:
                {
                    result.applied = left == sign && right == mask && fold.right_width == fold.width && fold.right_signed;
                    result.integer = fold.op == CHECK_OP_DIVIDE ? sign : 0;
                } break;
                default: result.applied = false; break;
            }
        }
        else if (fault != CHECK_FAULT_SIGNED_OVERFLOW && (fold.op == CHECK_OP_SHIFT_LEFT || fold.op == CHECK_OP_SHIFT_RIGHT))
        {
            const bool negative = fold.right_signed && (right >> (fold.right_width - 1)) != 0;
            const unsigned __int128 magnitude = negative ? (0 - right) & right_mask : right;
            const uint32_t amount = magnitude >= fold.width ? fold.width - 1 : (uint32_t)magnitude;
            const bool shift_left = (fold.op == CHECK_OP_SHIFT_LEFT) != negative;
            const bool negative_left = fold.is_signed && (left & sign) != 0;
            const bool invalid_count = negative || right >= fold.width;
            result.applied = fault == CHECK_FAULT_SHIFT_COUNT ? invalid_count :
                             !invalid_count && fold.op == CHECK_OP_SHIFT_LEFT && fold.is_signed &&
                             (negative_left || left > mask >> amount);
            if (shift_left)
            {
                result.integer = left << amount;
            }
            else
            {
                result.integer = left >> amount;
                if (negative_left)
                {
                    result.integer |= mask ^ (mask >> amount);
                }
            }
        }
        result.integer &= mask;
        result.fault = result.applied ? fault : CHECK_FAULT_NONE;
    }
    return result;
}

#endif
