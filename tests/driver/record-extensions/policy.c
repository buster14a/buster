#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wzero-length-array"
#pragma clang diagnostic warning "-Wc++-compat"
int first[0];
struct First {};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#pragma clang diagnostic ignored "-Wc++-compat"
int suppressed[0];
struct Suppressed {};
#pragma clang diagnostic pop
int restored[0];
struct Restored {};
#pragma clang diagnostic pop
int final[0];
struct Final {};
