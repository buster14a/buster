#pragma once
#include <buster/base.h>

STRUCT(FormatIntegerOptions)
{
    u64 value;
    IntegerFormat format;
    bool treat_as_signed;
    bool prefix;
    u16 reserved;
};

