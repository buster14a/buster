#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshift-count-overflow"
int a(int x){return x<<32;}
#pragma clang diagnostic pop
int b(int x){return x<<32;}
