int f(int x){switch(x){case 0:
__attribute__((fallthrough));
return 1;case 1:return x;default:return 0;}}
