int f(void){const int k=3;int a[k];_Static_assert(sizeof a==3*sizeof(int),"bound");return sizeof a;}
