int work(int);void f(int a,int b,int*p){a&&(work(b)+1);(work(b)+1)||a;a&&(work(b)==b);a&&p[work(b)];}
