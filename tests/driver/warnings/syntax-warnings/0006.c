int a(_Bool b){switch(b){case 0:return 1;default:return 0;}}
int b(int x){switch(x==1){case 0:return 1;default:return 0;}}
int c(int x){switch(!x){case 0:return 1;default:return 0;}}
int d(int x,int y){switch(x&&y){case 0:return 1;default:return 0;}}
int e(int x,int y){switch((x==1)|(y==2)){case 0:return 1;default:return 0;}}
int f(int x,int y){switch(x?y==1:y==2){case 0:return 1;default:return 0;}}
int g(int x,int y){switch((x++,y==1)){case 0:return 1;default:return 0;}}
int h(int x,int y){switch(x=(y==1)){case 0:return 1;default:return 0;}}
int i(_Bool x){switch(+x){case 0:return 1;default:return 0;}}
