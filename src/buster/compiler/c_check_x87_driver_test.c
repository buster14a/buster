// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// #1387: both tiers' long-double expression transfers through ELF and native execution. The separately
// compiled Clang harness compares ten-byte results, sticky exception flags and the control word in twelve x87 modes.
// Include after c_check_tier1_driver_test.c for its bounded object decoder.

static const char check_test_x87_source[] =
    "extern volatile int marker; long double x87_pending(void) { return 0.5L + (marker = 1, 2.0L); }\n"
    "void x87_add(long double *out, const long double *p) { *out = p[0] + p[1]; }\n"
    "void x87_chain(long double *out, const long double *p) { *out = p[0] * p[1] + p[2]; }\n"
    "void x87_order(long double *out, const long double *p) { *out = (p[0] - p[1]) / (p[2] - p[3]); }\n"
    "void x87_affine(long double *out, const long double *p) { *out = p[0] * 0.5L + p[1]; }\n"
    "void x87_literals(long double *out) {\n"
    "out[0]=0.0L; out[1]=-0.0L; out[2]=0.5L; out[3]=-1.5L;\n"
    "out[4]=0x1.0000000000000002p+0L; out[5]=0x1p-16382L; out[6]=0x1p-16445L; }\n"
    "long double x87_join(int choice) { return choice ? 0x1.0000000000000002p+0L : -0.5L; }\n"
    "long double x87_external(long double);\n"
    "long double x87_call(long double value) { return x87_external(value) + 0.5L; }\n"
    "void x87_trap(volatile int *marker, long double *out, const long double *p) { *out=p[0]*p[1]+p[2]; *marker=1; }\n"
    "void x87_alias(long double *p) { long double old = *p; *p = 2.0L; *p = old + *p; }\n";

static const char check_test_x87_harness[] =
    "#include <stdio.h>\n#include <string.h>\n#include <unistd.h>\n#include <signal.h>\n#include <sys/wait.h>\n"
    "void x87_add(long double*, const long double*); void x87_chain(long double*, const long double*);\n"
    "void x87_order(long double*, const long double*); void x87_affine(long double*, const long double*);\n"
    "void x87_literals(long double*); void x87_alias(long double*);\n"
    "void x87_trap(volatile int*, long double*, const long double*);\n"
    "long double x87_join(int); long double x87_call(long double);\n"
    "volatile int marker; long double x87_pending(void);\n"
    "static void pending_trapped(int s) { _exit(s==SIGFPE && marker==1 ? 0 : 92); }\n"
    "static volatile sig_atomic_t trap_marker;\n"
    "static long double trap_result=1.0L;\n"
    "static void trapped(int number) { static const unsigned char one[10]={0,0,0,0,0,0,0,128,255,63};\n"
    "const volatile unsigned char *bytes=(const volatile unsigned char*)&trap_result; unsigned changed=trap_marker!=0;\n"
    "for(unsigned i=0;i<10;++i) changed|=bytes[i]!=one[i]; _exit(number==SIGFPE && !changed ? 0 : 90); }\n"
    "long double x87_external(long double value) { return value * 2.0L; }\n"
    "static void reference(unsigned kind, long double *out, const long double *p) {\n"
    "if(kind==0) *out=p[0]+p[1]; else if(kind==1) *out=p[0]*p[1]+p[2];\n"
    "else if(kind==2) *out=(p[0]-p[1])/(p[2]-p[3]); else *out=p[0]*0.5L+p[1]; }\n"
    "int main(void) { alarm(10); unsigned errors=0; unsigned short saved,after;\n"
    "__asm__ volatile(\"fnstcw %0\":\"=m\"(saved));\n"
    "static const unsigned short precision[]={0,0x200,0x300};\n"
    "static const long double inputs[][4]={ {7.25L,-3.5L,0.125L,1.5L}, {0.0L,-0.0L,1.0L,2.0L},\n"
    "{0x1p-16382L,0x1p-16445L,0.5L,2.0L}, {0x1.fffffffffffffffep+16383L,2.0L,1.0L,0.5L} };\n"
    "void (*functions[])(long double*,const long double*)={x87_add,x87_chain,x87_order,x87_affine};\n"
    "for(unsigned p=0;p<3;++p) for(unsigned r=0;r<4;++r) {\n"
    "unsigned short control=(saved&~0xf00u)|0x3fu|precision[p]|(r<<10);\n"
    "__asm__ volatile(\"fnclex; fldcw %0\"::\"m\"(control));\n"
    "for(unsigned i=0;i<4;++i) for(unsigned k=0;k<4;++k) {\n"
    "long double actual,expected; unsigned short actual_flags,expected_flags;\n"
    "__asm__ volatile(\"fnclex\"); functions[k](&actual,inputs[i]);\n"
    "__asm__ volatile(\"fnstsw %0\":\"=a\"(actual_flags));\n"
    "__asm__ volatile(\"fnclex\"); reference(k,&expected,inputs[i]);\n"
    "__asm__ volatile(\"fnstsw %0\":\"=a\"(expected_flags));\n"
    "errors+=memcmp(&actual,&expected,10)!=0; errors+=(actual_flags&0x3f)!=(expected_flags&0x3f);\n"
    "__asm__ volatile(\"fnstcw %0\":\"=m\"(after)); errors+=after!=control; }\n"
    "long double values[7]; static const long double constants[]={0.0L,-0.0L,0.5L,-1.5L,0x1.0000000000000002p+0L,0x1p-16382L,0x1p-16445L};\n"
    "x87_literals(values); for(unsigned i=0;i<7;++i) errors+=memcmp(values+i,constants+i,10)!=0; }\n"
    "__asm__ volatile(\"fnclex; fldcw %0\"::\"m\"(saved));\n"
    "errors+=x87_join(0)!=-0.5L; errors+=x87_join(1)!=0x1.0000000000000002p+0L;\n"
    "errors+=x87_call(3.25L)!=7.0L; long double alias=3.25L; x87_alias(&alias); errors+=alias!=5.25L;\n"
    "pid_t child=fork(); if(child==0) { alarm(5); signal(SIGFPE,trapped);\n"
    "unsigned short unmasked=(saved|0x3fu)&~8u;\n"
    "__asm__ volatile(\"fnclex; fldcw %0\"::\"m\"(unmasked));\n"
    "x87_trap(&trap_marker,&trap_result,inputs[3]); _exit(91); }\n"
    "int child_status=0; if(child<0) ++errors; else {\n"
    "errors+=waitpid(child,&child_status,0)!=child; errors+=!WIFEXITED(child_status)||WEXITSTATUS(child_status)!=0; }\n"
    "child=fork(); if(child==0) { alarm(5); signal(SIGFPE,pending_trapped); const unsigned short cw=0x037e;\n"
    "__asm__ volatile(\"fninit; fldz; fldz; fdivp; fldcw %0\"::\"m\"(cw):\"memory\"); (void)x87_pending(); _exit(93); }\n"
    "if(child<0) ++errors; else { errors+=waitpid(child,&child_status,0)!=child; errors+=!WIFEXITED(child_status)||WEXITSTATUS(child_status)!=0; }\n"
    "printf(\"%u\\n\",errors); return errors!=0; }\n";

static void check_test_driver_x87(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char harness[512];
    char object[512];
    char output[512];
    snprintf(source, sizeof(source), "%s/x87-transfers.c", directory);
    snprintf(harness, sizeof(harness), "%s/x87-transfers-main.c", directory);
    snprintf(object, sizeof(object), "%s/x87-transfers.o", directory);
    snprintf(output, sizeof(output), "%s/x87-transfers", directory);
    const bool written = parse_test_write(source, check_test_x87_source, sizeof(check_test_x87_source) - 1) &&
                         parse_test_write(harness, check_test_x87_harness, sizeof(check_test_x87_harness) - 1);
    check_test_expect(test, written, "long double transfer driver sources written");
    for (uint32_t mode = 0; written && mode < 6; mode += 1)
    {
        const bool scalar = (mode & 1) != 0;
        const bool tier1 = mode >= 2;
        const bool scheduled = mode >= 4;
        char* arguments[20] = { (char*)executable, "--verify" };
        uint32_t count = 2;
        if (scalar)
        {
            arguments[count++] = "--scalar";
        }
        if (tier1)
        {
            arguments[count++] = "--tier1";
        }
        if (scheduled)
        {
            arguments[count++] = "--tier1-schedule=post";
        }
        arguments[count++] = "-std=gnu17";
        arguments[count++] = "-w";
        arguments[count++] = "-c";
        arguments[count++] = "-o";
        arguments[count++] = object;
        arguments[count++] = source;
        arguments[count] = NULL;
        char* text = NULL;
        int status = check_test_driver_spawn(arguments, &text);
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        if (status == 0)
        {
            char* link_arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-ffp-contract=off", "-fno-strict-aliasing", "-o", output, harness, object, NULL };
            text = NULL;
            status = check_test_driver_spawn(link_arguments, &text);
            if (status != 0 && text != NULL)
            {
                fprintf(stderr, "%s", text);
            }
            free(text);
        }
        char captured[128] = "";
        const bool ran = status == 0 && check_test_driver_capture(output, captured, sizeof(captured));
        char description[256];
        snprintf(description, sizeof(description), "long double values, flags, controls and literal bytes (%s, Tier %u, %s)",
                 scalar ? "scalar" : "vector", tier1 ? 1 : 0, scheduled ? "post-scheduled" : "original order");
        check_test_expect(test, ran && strcmp(captured, "0\n") == 0, description);
        if (status == 0 && (!ran || strcmp(captured, "0\n") != 0))
        {
            fprintf(stderr, "%s: %s", description, captured);
        }
        unlink(output);
        unlink(object);
    }
    unlink(harness);
    unlink(source);
}
