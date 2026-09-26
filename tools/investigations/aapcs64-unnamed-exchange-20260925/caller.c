#include "contract.h"

unsigned run_case(unsigned row)
{
    unsigned result = 0;
    unsigned tag = 0x10203040u;
    switch (row)
    {
        case 0:
        {
            struct plain_nested value = {0};
            value.lead = 1; value.inner.a = 2; value.inner.b = 3; value.tail = 4;
            result = consume_plain(value, tag);
        } break;
        case 1:
        {
            struct zero value = {0};
            value.a = 2; value.b = 3;
            result = consume_single(value, tag);
        } break;
        case 2:
        {
            struct zero_nested value = {0};
            value.lead = 1; value.inner.a = 2; value.inner.b = 3; value.tail = 4;
            result = consume_nested(value, tag);
        } break;
        case 3:
        {
            struct aligned_nested value = {0};
            value.lead = 1; value.inner.a = 2; value.inner.b = 3; value.tail = 4;
            result = consume_aligned(value, tag);
        } break;
        case 4:
        {
            struct zero_pair value = {0};
            value.inner[0].a = 1; value.inner[0].b = 2;
            value.inner[1].a = 3; value.inner[1].b = 4;
            result = consume_pair(value, tag);
        } break;
    }
    return result;
}

unsigned producer_size(unsigned row)
{
    unsigned result = 0;
    switch (row)
    {
        case 0: result = sizeof(struct plain_nested); break;
        case 1: result = sizeof(struct zero); break;
        case 2: result = sizeof(struct zero_nested); break;
        case 3: result = sizeof(struct aligned_nested); break;
        case 4: result = sizeof(struct zero_pair); break;
    }
    return result;
}
