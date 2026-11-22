// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "driver.h"
int main(void)
{
    struct FeedbackPair value = { 4, 9 };
    int (*function)(struct FeedbackPair) = feedback_sum;
    int total = 0;
    for (int i = 0; i < 5; i += 1)
    {
        total += i;
    }
    return function(value) != 20 || total != 10;
}
