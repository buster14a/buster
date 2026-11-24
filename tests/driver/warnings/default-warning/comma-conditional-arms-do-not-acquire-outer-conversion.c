int side(void);void take(int);int f(int c){int x=c?(side(),2.5):3.0;x=c?3.0:(side(),2.5);take(c?(side(),2.5):3.0);return c?(side(),2.5):3.0;}
