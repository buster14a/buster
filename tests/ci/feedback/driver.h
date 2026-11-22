// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef BUSTER_FEEDBACK_DRIVER_H
#define BUSTER_FEEDBACK_DRIVER_H
#define BUSTER_FEEDBACK_BIAS 7
struct FeedbackPair { int x; int y; };
int feedback_sum(struct FeedbackPair value);
#endif
