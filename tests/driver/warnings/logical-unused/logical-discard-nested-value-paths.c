int work(int);void f(int a,int b){a&&(b||work(a));a||(b&&work(a));(work(a)&&b)||a;(work(a)||b)&&a;}
