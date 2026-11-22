// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "driver.h"
int feedback_sum(struct FeedbackPair value)
{
    return value.x + value.y + BUSTER_FEEDBACK_BIAS;
}
