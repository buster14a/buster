typedef int Required __attribute__((warn_unused_result));typedef Required Alias;Alias work(int);void f(int a){a&&work(a);}
