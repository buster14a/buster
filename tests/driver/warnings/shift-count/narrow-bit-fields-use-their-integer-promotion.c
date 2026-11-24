struct S{unsigned long n:7;};int a(struct S x){return x.n<<31;}int b(struct S x){return x.n>>32;}
