int a(_Bool b){switch((int)b){case 0:return 1;default:return 0;}}
int b(int x){switch(x){case 0:return 1;default:return 0;}}
int c(void){switch(1){case 0:return 1;default:return 0;}}
int d(int x,int y){switch((x==1)|y){case 0:return 1;default:return 0;}}
int e(int x,int y){switch(x|=(y==1)){case 0:return 1;default:return 0;}}
