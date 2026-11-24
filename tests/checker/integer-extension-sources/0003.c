typedef _BitInt((8>>-1)+1) I; enum { A=1<<-1, B=8>>32, C=-8<<-1 }; _Static_assert(sizeof(I)==4 && A==0 && B==0 && C==-4,"reversed shifts");
