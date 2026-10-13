// Trusted reference init for #3186. Compile independently with GCC/glibc.
// Seven named controls cover only this declared minimal configuration.
// main reports the denominator, then requests a guest-only clean poweroff.
#include <buster/lib/base.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <netinet/in.h>
#include <time.h>

BUSTER_GLOBAL_LOCAL volatile sig_atomic_t signal_seen;
BUSTER_GLOBAL_LOCAL void control_signal(int value)
{
    signal_seen = value;
}

BUSTER_GLOBAL_LOCAL bool file_control(void)
{
    bool valid = mount("tmpfs", "/tmp", "tmpfs", 0, "size=4m") == 0;
    int fd = valid ? open("/tmp/control", O_CREAT | O_EXCL | O_RDWR, 0600) : -1;
    char actual[8] = {0};
    if (fd >= 0)
    {
        valid = write(fd, "kernel", 6) == 6 && fsync(fd) == 0 &&
                lseek(fd, 0, SEEK_SET) == 0 && read(fd, actual, 6) == 6 &&
                memcmp(actual, "kernel", 6) == 0;
        valid = close(fd) == 0 && valid;
        valid = unlink("/tmp/control") == 0 && valid;
    }
    else
    {
        valid = false;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool memory_control(void)
{
    size_t bytes = 16 * 4096;
    unsigned char* p = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = p != MAP_FAILED;
    if (valid)
    {
        for (size_t i = 0; i < bytes; ++i)
        {
            p[i] = (unsigned char)(i * 17u + 3u);
        }
        for (size_t i = 0; i < bytes; ++i)
        {
            valid = valid && p[i] == (unsigned char)(i * 17u + 3u);
        }
        valid = munmap(p, bytes) == 0 && valid;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool process_control(void)
{
    pid_t child = fork();
    bool valid = child >= 0;
    if (child == 0)
    {
        _exit(37);
    }
    else if (child > 0)
    {
        int status = 0;
        valid = waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 37;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool pipe_control(void)
{
    int pipes[2] = {-1, -1};
    bool valid = pipe(pipes) == 0;
    if (valid)
    {
        char actual[4] = {0};
        valid = write(pipes[1], "sync", 4) == 4 && read(pipes[0], actual, 4) == 4 &&
                memcmp(actual, "sync", 4) == 0;
        valid = close(pipes[0]) == 0 && valid;
        valid = close(pipes[1]) == 0 && valid;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool timer_control(void)
{
    struct timespec before = {0}, after = {0}, delay = {.tv_nsec = 10000000};
    bool valid = clock_gettime(CLOCK_MONOTONIC, &before) == 0 && nanosleep(&delay, 0) == 0 &&
                 clock_gettime(CLOCK_MONOTONIC, &after) == 0;
    valid = valid && (after.tv_sec > before.tv_sec ||
                     (after.tv_sec == before.tv_sec && after.tv_nsec > before.tv_nsec));
    return valid;
}

BUSTER_GLOBAL_LOCAL bool signal_control(void)
{
    struct sigaction action = {0};
    action.sa_handler = control_signal;
    bool valid = sigemptyset(&action.sa_mask) == 0 && sigaction(SIGUSR1, &action, 0) == 0 &&
                 kill(getpid(), SIGUSR1) == 0 && signal_seen == SIGUSR1;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool network_control(void)
{
    int server = socket(AF_INET, SOCK_DGRAM, 0);
    int client = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t length = sizeof(address);
    bool valid = server >= 0 && client >= 0 && bind(server, (struct sockaddr*)&address, length) == 0 &&
                 getsockname(server, (struct sockaddr*)&address, &length) == 0;
    // Loopback is enabled explicitly by the host harness configuration via
    // SIOCSIFFLAGS below; no guest packets reach the cloud host's network.
    if (valid)
    {
        struct timeval deadline = {.tv_sec = 2};
        char actual[3] = {0};
        valid = setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline)) == 0 &&
                sendto(client, "net", 3, 0, (struct sockaddr*)&address, length) == 3 &&
                recv(server, actual, 3, 0) == 3 && memcmp(actual, "net", 3) == 0;
    }
    if (server >= 0)
    {
        valid = close(server) == 0 && valid;
    }
    if (client >= 0)
    {
        valid = close(client) == 0 && valid;
    }
    return valid;
}

#include <sys/ioctl.h>
#include <net/if.h>
BUSTER_GLOBAL_LOCAL bool enable_loopback(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq request = {0};
    memcpy(request.ifr_name, "lo", 3);
    bool valid = fd >= 0 && ioctl(fd, SIOCGIFFLAGS, &request) == 0;
    if (valid)
    {
        request.ifr_flags |= IFF_UP;
        valid = ioctl(fd, SIOCSIFFLAGS, &request) == 0;
    }
    if (fd >= 0)
    {
        valid = close(fd) == 0 && valid;
    }
    return valid;
}

int main(void)
{
    bool results[7];
    char const* names[] = {"tmpfs-file", "anonymous-memory", "fork-wait", "pipe", "timer", "signal", "loopback-udp"};
    unsigned passed = 0;
    setvbuf(stdout, 0, _IONBF, 0);
    results[0] = file_control();
    results[1] = memory_control();
    results[2] = process_control();
    results[3] = pipe_control();
    results[4] = timer_control();
    results[5] = signal_control();
    results[6] = enable_loopback() && network_control();
    for (unsigned i = 0; i < 7; ++i)
    {
        printf("LINUX_CONTROL name=%s result=%s\n", names[i], results[i] ? "pass" : "fail");
        passed += results[i] ? 1u : 0u;
    }
    printf("LINUX_CONTROL_SUMMARY attempted=7 passed=%u failed=%u skipped=0\n", passed, 7u - passed);
    sync();
    if (passed == 7)
    {
        reboot(RB_POWER_OFF);
    }
    // A failed reboot remains observable and never supplies a success marker.
    printf("LINUX_CONTROL_POWEROFF_FAILED errno=%d\n", errno);
    return 1;
}
