_Pragma("clang assume_nonnull begin")
#define SILENT 1
_Pragma("clang assume_nonnull end")
int first = __LINE__;
#pragma clang assume_nonnull begin
int *pointer;
#pragma clang assume_nonnull end
int last = __LINE__;
