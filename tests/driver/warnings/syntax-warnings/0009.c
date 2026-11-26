#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wparentheses"
#pragma clang diagnostic ignored "-Wswitch-bool"
int a(int x){if((x==1))return !x&1;return 0;}
int b(_Bool x){switch(x){case 0:return 1;default:return 0;}}
#pragma clang diagnostic pop
int c(int x){if((x==1))return !x&1;return 0;}
int d(_Bool x){switch(x){case 0:return 1;default:return 0;}}
