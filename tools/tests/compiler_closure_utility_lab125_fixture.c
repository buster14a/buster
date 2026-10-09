// Private hosted Utility lab failure: actual exit125 and an escaped descendant.
// This is never a benchmark or an approved-host entry. Native owners must reap
// the descendant and stop all later measurement before retaining failed data.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int fixture_host_allowed(void)
{
    extern char** environ;
    int result=1;
    for (char** item=environ; result && item && *item; item+=1)
        if (strncmp(*item,"BQ_",3)==0) result=strcmp(*item,"BQ_REQUIRE_DISTINCT_GROUP=1")==0;
    static char cpu[(1u<<20)+1];
    size_t used=0,limit=sizeof(cpu)-1;
    int descriptor=result ? open("/proc/cpuinfo",O_RDONLY|O_CLOEXEC|O_NOFOLLOW) : -1;
    result=result && descriptor>=0;
    int eof=0;
    while (result && !eof)
    {
        ssize_t count=read(descriptor,cpu+used,limit+1-used);
        if (count>0) { used+=(size_t)count; result=used<=limit; }
        else if (count==0) eof=1;
        else result=errno==EINTR;
    }
    if (descriptor>=0) result=close(descriptor)==0 && result;
    if (used<=limit) cpu[used]=0;
    result=result && eof && used && strstr(cpu,"model name")!=NULL;
    const char* needle="9700x";
    for (size_t i=0; result && i+5<=used; i+=1)
    {
        int matched=1;
        for (size_t j=0; matched && j<5; j+=1)
        {
            unsigned char byte=(unsigned char)cpu[i+j];
            if (byte>='A' && byte<='Z') byte=(unsigned char)(byte-'A'+'a');
            matched=byte==(unsigned char)needle[j];
        }
        if (matched) result=0;
    }
    return result;
}

static int fixture_source_allowed(const char* root,const char* output)
{
    int result=root && output && root[0]=='/' && output[0]=='/' && strlen(root)<4096 && strlen(output)<4096;
    const char* selection=getenv("BUSTER_UTILITY_DIAGNOSTIC_LAB_CASE");
    result=result && selection && (strcmp(selection,"legacy")==0 || strcmp(selection,"snapshot")==0);
    const char* slash=result ? strrchr(root,'/') : NULL;
    result=result && slash && strcmp(slash,"/source")==0;
    char path[8192],expected[8192];
    if (result)
    {
        int count=snprintf(expected,sizeof(expected),"%.*s/output/%s-work/lab",(int)(slash-root),root,selection);
        result=count>0 && (size_t)count<sizeof(expected) && strcmp(expected,output)==0;
    }
    if (result)
    {
        int count=snprintf(path,sizeof(path),"%s/.compiler-utility-fixture",root);
        result=count>0 && (size_t)count<sizeof(path);
    }
    int descriptor=result ? open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW) : -1;
    struct stat status={0};
    result=result && descriptor>=0 && fstat(descriptor,&status)==0 && S_ISREG(status.st_mode);
    char marker[128]={0};
    ssize_t count=result ? read(descriptor,marker,sizeof(marker)) : -1;
    const char* expected_marker="BUSTER_COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_ONLY_V1\n";
    result=result && count==(ssize_t)strlen(expected_marker) && memcmp(marker,expected_marker,(size_t)count)==0;
    if (descriptor>=0) result=close(descriptor)==0 && result;
    return result;
}

struct FixtureReport { pid_t process,group,session; };

int main(int argc,char** argv)
{
    int result=1;
    int allowed=argc==3 && fixture_host_allowed() && fixture_source_allowed(argv[1],argv[2]);
    int descriptors[2]={-1,-1};
    allowed=allowed && pipe(descriptors)==0 && descriptors[1]>=3;
    pid_t child=allowed ? fork() : -1;
    if (child==0)
    {
        close(descriptors[0]);
        // The escaped child must not keep the native owner's captured pipes
        // open. Ready is published only after both capture streams are closed.
        close(STDIN_FILENO);
        int streams_closed=close(STDOUT_FILENO)==0;
        streams_closed=close(STDERR_FILENO)==0 && streams_closed;
        struct sigaction action={0};
        action.sa_handler=SIG_IGN;
        int ready=streams_closed && setsid()==getpid() && sigemptyset(&action.sa_mask)==0 && sigaction(SIGTERM,&action,NULL)==0;
        struct FixtureReport report={getpid(),getpgrp(),getsid(0)};
        ready=ready && report.process==report.group && report.process==report.session &&
            write(descriptors[1],&report,sizeof(report))==(ssize_t)sizeof(report);
        close(descriptors[1]);
        if (!ready) _exit(7);
        for (;;) pause();
    }
    if (child>1)
    {
        close(descriptors[1]); descriptors[1]=-1;
        struct pollfd event={.fd=descriptors[0],.events=POLLIN};
        struct FixtureReport report={0};
        int ready=poll(&event,1,2000)>0 && read(descriptors[0],&report,sizeof(report))==(ssize_t)sizeof(report) &&
            report.process==child && report.group==child && report.session==child && child!=getpgrp();
        if (ready)
        {
            int printed=printf("COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_LAB125 parent_pid=%ld escaped_pid=%ld "
                "escaped_group=%ld escaped_session=%ld term_ignored=1 exit=125 physical_qualification=false\n",
                (long)getpid(),(long)report.process,(long)report.group,(long)report.session);
            result=printed>0 && fflush(stdout)==0 ? 125:1;
        }
    }
    for (size_t i=0;i<2;i+=1) if (descriptors[i]>=0) close(descriptors[i]);
    return result;
}
