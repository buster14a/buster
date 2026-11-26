void work(int*)__attribute__((nonnull(1)));void f(void){
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonnull"
work(0);
#pragma clang diagnostic pop
work(0);}
