int work(int);void f(int a,int b,int c){a&&(b?work(a):c);a&&(b?c:work(a));a&&(b?:work(a));b?(a&&work(a)):a;}
