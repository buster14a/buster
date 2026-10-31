// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original literal-range warning facts. The reader owns appending and source policy; this helper allocates nothing.
// Include after c_check_warning_events.c and CheckLiteral.

static __attribute__((unused)) bool check_literal_warning_event(const CheckLiteral* literal, uint32_t token, CheckWarningEvent* event)
{
    const bool overflow = literal->range == CHECK_LITERAL_RANGE_OVERFLOW;
    const bool underflow = literal->range == CHECK_LITERAL_RANGE_UNDERFLOW;
    const bool result = literal->valid && literal->real && (overflow || underflow);
    if (result)
    {
        const TypeId type = literal->imaginary ? BUSTER_TYPE_BUILTIN(type_index(literal->type) - BUSTER_TYPE_CODE_COMPLEX) : literal->type;
        *event = (CheckWarningEvent)
        {
            .id = CHECK_WARNING_ID_LITERAL_RANGE,
            .kind = overflow ? CHECK_WARNING_EVENT_LITERAL_OVERFLOW : CHECK_WARNING_EVENT_LITERAL_UNDERFLOW,
            .token = token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = token,
            .payload = { .first_type = type },
        };
    }
    return result;
}
