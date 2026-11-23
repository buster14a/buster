// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

const int logical_eval_value = 4;
const int logical_eval_array[2] = { 4, 8 };

struct LogicalEvalRecord
{
    int member;
};

const struct LogicalEvalRecord logical_eval_record = { 16 };
int logical_eval_call(void);

int logical_const_arithmetic(int x)
{
    const int local = 8;
    return (x && (logical_eval_value + 1)) + (x || (unsigned char)local);
}

int logical_const_designators(int x)
{
    return (x && logical_eval_array[1]) + (x || logical_eval_record.member) + (x && *(&logical_eval_value));
}

int logical_const_comma(int x)
{
    return x && (logical_eval_value, 4);
}

int logical_runtime_comma(int x)
{
    return x || (x, 8);
}

int logical_effectful_comma(int x, int y)
{
    return (x && (logical_eval_call(), 4)) + (x || (y = 1, 8));
}

int logical_statement_values(int x)
{
    return (x && ({ 4; })) + (x || ({ const int local = 8; local; }));
}

int logical_runtime_statement_values(int x)
{
    return (x && ({ logical_eval_call(); 4; })) + (x || ({ int local = 8; local; })) + (x && ({ x; 4; }));
}

int logical_untaken_calls(int x)
{
    return (x && (0 ? logical_eval_call() : 4)) + (x || (1 ? 8 : logical_eval_call()));
}

int logical_short_circuit_calls(int x)
{
    return (x && ((0 && logical_eval_call()) + 4)) + (x || ((1 || logical_eval_call()) + 7));
}

int logical_selected_calls(int x)
{
    return (x && __builtin_choose_expr(1, 4, logical_eval_call())) +
           (x || _Generic(x, int: 8, default: logical_eval_call()));
}
