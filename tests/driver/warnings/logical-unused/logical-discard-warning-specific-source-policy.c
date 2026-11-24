int work(int);void f(int a,int b){
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
a&&b;
#pragma clang diagnostic pop
a&&work(a);a&&b;}
