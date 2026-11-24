int d1(int x){return 0;if((1<<40)!=0){}}
int n1(int x){return _Generic(x,int:0,long:0);}
int d2(int x){goto out;if((1<<41)!=0){}out:return x;}
int d3(int x){while(x){return 1;if((1<<42)!=0){}}return 0;}
int d4(int x){switch(x){case 1:return 1;if((1<<43)!=0){}case 2:if((1<<44)!=0){}}return 0;}
int d5(int x){while(1){if(x)return 1;}if((1<<45)!=0){}}
int r1(int x){if(x)return 1;if((1<<46)!=0){}return 0;}
int r2(int x){goto l;return 1;l:if((1<<47)!=0){}return 0;}
int r3(int x){while(x){if(x>2)break;if((1<<48)!=0){}}return 0;}
int r4(int x){for(;;){if(x)break;}if((1<<49)!=0){}return 0;}
int r5(int x){if(x){x=2;}else{return 1;}if((1<<50)!=0){}return x;}
int r6(int x){if((1<<51)!=0){}}
