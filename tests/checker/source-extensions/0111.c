int f(void){int a[]={[3]=7,9}; _Static_assert(sizeof a==20,"extent"); return a[4];}
