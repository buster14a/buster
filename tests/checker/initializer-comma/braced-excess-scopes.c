#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexcess-initializers"
int ignored[1] = { 0, 1 };
#pragma clang diagnostic pop

#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wexcess-initializers"
int warning[1] = { 0, 1 };
#pragma clang diagnostic pop

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wexcess-initializers"
int error[1] = { 0, 1 };
#pragma clang diagnostic pop

int restored[1] = { 0, 1 };
