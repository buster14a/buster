int f(void){return sizeof((int){1.5})+_Generic(0,int:0,default:(int){2.5});}
