#include "contract.h"

unsigned observed_payload;
unsigned observed_tag;

unsigned consume_plain(struct plain_nested value, unsigned tag)
{
    observed_payload = (unsigned)value.lead | ((unsigned)value.inner.a << 8)
                     | ((unsigned)value.inner.b << 16) | ((unsigned)value.tail << 24);
    observed_tag = tag;
    return observed_payload ^ tag;
}
unsigned consume_single(struct zero value, unsigned tag)
{
    observed_payload = (unsigned)value.a | ((unsigned)value.b << 8);
    observed_tag = tag;
    return observed_payload ^ tag;
}
unsigned consume_nested(struct zero_nested value, unsigned tag)
{
    observed_payload = (unsigned)value.lead | ((unsigned)value.inner.a << 8)
                     | ((unsigned)value.inner.b << 16) | ((unsigned)value.tail << 24);
    observed_tag = tag;
    return observed_payload ^ tag;
}
unsigned consume_aligned(struct aligned_nested value, unsigned tag)
{
    observed_payload = (unsigned)value.lead | ((unsigned)value.inner.a << 8)
                     | ((unsigned)value.inner.b << 16) | ((unsigned)value.tail << 24);
    observed_tag = tag;
    return observed_payload ^ tag;
}
unsigned consume_pair(struct zero_pair value, unsigned tag)
{
    observed_payload = (unsigned)value.inner[0].a | ((unsigned)value.inner[0].b << 8)
                     | ((unsigned)value.inner[1].a << 16) | ((unsigned)value.inner[1].b << 24);
    observed_tag = tag;
    return observed_payload ^ tag;
}
unsigned consumer_size(unsigned row)
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
