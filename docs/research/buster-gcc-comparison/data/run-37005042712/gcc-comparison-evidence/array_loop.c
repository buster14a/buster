extern int printf(const char *, ...);
int main(void) { unsigned int a[1024]; for(unsigned int i=0;i<1024;i++)a[i]=i; for(unsigned int r=0;r<10000;r++)for(unsigned int i=0;i<1024;i++)a[i]=(a[i]+r)*1664525u+1013904223u; unsigned int s=0;for(unsigned int i=0;i<1024;i++)s+=a[i];printf("%u\n",s);return 0;}
