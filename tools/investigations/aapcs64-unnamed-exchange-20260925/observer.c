/* This translation unit NEVER includes contract.h or declares a suspect record.
 * Expected integers were derived before Buster execution, not from its sizeof,
 * metadata, caller, callee, output, or an oracle compiler's layout dump.
 */
#include <limits.h>
#include <stdio.h>
extern unsigned run_case(unsigned row);
extern unsigned producer_size(unsigned row);
extern unsigned consumer_size(unsigned row);
extern unsigned observed_payload;
extern unsigned observed_tag;

int main(void)
{
    unsigned mismatches = 0;
    int result = 2;
    unsigned const payloads[5] = {0x04030201u, 0x00000302u, 0x04030201u, 0x04030201u, 0x04030201u};
    unsigned const results[5] = {0x14233241u, 0x10203342u, 0x14233241u, 0x14233241u, 0x14233241u};
    if (CHAR_BIT == 8 && UINT_MAX == 0xffffffffu && sizeof(unsigned) == 4)
    {
        for (unsigned row = 0; row < 5; row += 1)
        {
            observed_payload = 0xeeeeeeeeu;
            observed_tag = 0xeeeeeeeeu;
            unsigned actual_result = run_case(row);
            unsigned expected_result = results[row];
#ifdef WRONG_EXPECTATION
            if (row == 0)
            {
                expected_result ^= 1u;
            }
#endif
            mismatches += observed_payload != payloads[row];
            mismatches += observed_tag != 0x10203040u;
            mismatches += actual_result != expected_result;
            printf("row=%u payload=%08x/%08x tag=%08x/10203040 result=%08x/%08x sizes=%u/%u\n",
                   row, observed_payload, payloads[row], observed_tag, actual_result,
                   expected_result, producer_size(row), consumer_size(row));
        }
        printf("SUMMARY rows=5 cells=15 mismatches=%u\n", mismatches);
        result = mismatches != 0;
    }
    return result;
}
