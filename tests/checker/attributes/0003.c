int f(int x){switch(x){case 0:x++;[[gnu::fallthrough]];case 1:return x;default:return 0;}}
