// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original transient probe for issue 1447 packed-boolean builtin semantics.
#include <stdio.h>
static void fill(void*p,unsigned long n,unsigned s){unsigned char*b=p;for(unsigned long i=0;i<n;i++){s=s*1103515245u+12345u;b[i]=(unsigned char)(s>>16);}}
static unsigned errors;
#define CASE(N) static unsigned bits##N(unsigned h,int n) { \
 typedef _Bool V __attribute__((ext_vector_type(N))); V a,b,c; \
 fill(&a,sizeof a,n);fill(&b,sizeof b,n+1);fill(&c,sizeof c,n+2); \
 V r[]={__builtin_elementwise_max(a,b),__builtin_elementwise_min(a,b), \
 __builtin_elementwise_add_sat(a,b),__builtin_elementwise_sub_sat(a,b), \
 __builtin_elementwise_popcount(a),__builtin_elementwise_bitreverse(a), \
 __builtin_elementwise_fshl(a,b,c),__builtin_elementwise_fshr(a,b,c), \
 __builtin_elementwise_pdep(a,b),__builtin_elementwise_pext(a,b),__builtin_elementwise_clmul(a,b)}; \
 for(unsigned j=0;j<11;j++)for(unsigned i=0;i<N;i++){ \
  unsigned x=a[i],y=b[i],expected=j==0||j==2?(x|y):j==1||j>=8?(x&y):j==3?(x&!y):j==7?y:x; \
  unsigned actual=r[j][i]; h=(h^actual)*16777619u; \
  if(actual!=expected){if(errors<40)printf("MISMATCH n=%d N=%d builtin=%u lane=%u a=%u b=%u actual=%u expected=%u\n",n,N,j,i,x,y,actual,expected);errors++;} \
 } return h; }
CASE(1)CASE(3)CASE(8)CASE(12)CASE(32)CASE(64)CASE(65)CASE(128)CASE(1024)
int main(void){for(int n=-3;n<=19;n++){unsigned h=2166136261u;
 h=bits1(h,n);h=bits3(h,n+1);h=bits8(h,n+2);h=bits12(h,n+3);h=bits32(h,n+4);
 h=bits64(h,n+5);h=bits65(h,n+6);h=bits128(h,n+7);h=bits1024(h,n+8);
 printf("HASH n=%d value=%u\n",n,h);}printf("ERRORS=%u\n",errors);return errors!=0;}
