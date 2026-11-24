typedef unsigned short Word; typedef Word *Ptr;
int fold(Word first, const Word *next, Ptr *last);
int fold(Word first,const Word *next,Ptr *last){ Word local=first; const Word *p=next;
  {typedef long Word; Word extra=**last; local+=extra;} return local+*p; }
