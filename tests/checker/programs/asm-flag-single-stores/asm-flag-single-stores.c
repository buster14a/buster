#define _GNU_SOURCE
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
static volatile int faults;static unsigned char* page;
static void on_write(int signal,siginfo_t* info,void* context){(void)signal;(void)info;faults+=1;mprotect(page,4096,PROT_READ|PROT_WRITE);((ucontext_t*)context)->uc_mcontext.gregs[REG_EFL]|=0x100;}
static void on_step(int signal,siginfo_t* info,void* context){(void)signal;(void)info;mprotect(page,4096,PROT_READ);((ucontext_t*)context)->uc_mcontext.gregs[REG_EFL]&=~0x100;}
int tested(int n){struct sigaction write_action={0},step_action={0},old_write,old_step;
write_action.sa_sigaction=on_write;write_action.sa_flags=SA_SIGINFO;step_action.sa_sigaction=on_step;step_action.sa_flags=SA_SIGINFO;
page=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(page==MAP_FAILED)return -1;
sigaction(SIGSEGV,&write_action,&old_write);sigaction(SIGTRAP,&step_action,&old_step);
volatile unsigned* v=(volatile unsigned*)page;_Atomic unsigned* a=(_Atomic unsigned*)(page+64);volatile unsigned short* s=(volatile unsigned short*)(page+128);
volatile unsigned long* l=(volatile unsigned long*)(page+192);volatile unsigned char* c=(volatile unsigned char*)(page+256);_Atomic unsigned long* al=(_Atomic unsigned long*)(page+320);
unsigned h=(unsigned)n;
for(int i=-2;i<3;i++){int x=i+n;*v=7;*a=7;*s=7;*l=7;*c=7;*al=7;mprotect(page,4096,PROT_READ);faults=0;
__asm__ volatile("testl %1,%1" : "=@ccz"(*v) : "r"(x) : "cc");h=h*31+(unsigned)faults;faults=0;
__asm__ volatile("testl %1,%1" : "=@ccnz"(*a) : "r"(x) : "cc");h=h*31+(unsigned)faults;faults=0;
__asm__ volatile("cmpl $1,%2" : "=@ccl"(*s), "=@ccg"(*l) : "r"(x) : "cc");h=h*31+(unsigned)faults;faults=0;
__asm__ volatile("cmpl $0,%1" : "=@ccs"(*al) : "r"(x) : "cc");h=h*31+(unsigned)faults;faults=0;
__asm__ goto("testl %1,%1\n\tjs %l[negative]" : "=@ccz"(*c) : "r"(x) : "cc" : negative);h=h*31+(unsigned)faults*7;goto done;
negative:h=h*31+(unsigned)faults*11;
done:mprotect(page,4096,PROT_READ|PROT_WRITE);h=h*31+*v+*a*2+*s*4+(unsigned)*l*8+*c*16+(unsigned)*al*32;}
sigaction(SIGSEGV,&old_write,0);sigaction(SIGTRAP,&old_step,0);munmap(page,4096);return (int)h;}
