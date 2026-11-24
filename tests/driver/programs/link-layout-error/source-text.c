char gbig[3L << 30]; int main(void) { gbig[(3L << 30) - 1] = 42; return gbig[(3L << 30) - 1] != 42; }
