int tested(int n){int z;
#ifdef __GCC_ASM_FLAG_OUTPUTS__
__asm__("testl %1,%1" : "=@ccz"(z) : "r"(n) : "cc");
#else
__asm__("testl %1,%1; sete %b0; movzbl %b0,%0" : "=r"(z) : "r"(n) : "cc");
#endif
return z;}