#define ENUM(name) enum { name }
enum Named { N };
ENUM(U) untyped;
typedef ENUM(T) Alias;
int f(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wanon-enum-enum-conversion"
#pragma clang diagnostic warning "-Wenum-compare"
    int result = (N == U) + (N == T);
#pragma clang diagnostic ignored "-Wenum-compare"
#pragma clang diagnostic warning "-Wanon-enum-enum-conversion"
    result += (N == U) + (N == T);
#pragma clang diagnostic pop
    return result;
}
