#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

static void daemonize()
{
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("fork");
        std::exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        std::exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        std::perror("setsid");
        std::exit(EXIT_FAILURE);
    }

    if (chdir("/") < 0) {
        std::perror("chdir");
        std::exit(EXIT_FAILURE);
    }

    long maxFd = sysconf(_SC_OPEN_MAX);
    if (maxFd < 0 || maxFd > 4096) {
        maxFd = 4096;
    }
    for (int fd = 0; fd < maxFd; ++fd) {
        close(fd);
    }

    int fd0 = open("/dev/null", O_RDWR);
    if (fd0 != 0) {
        std::exit(EXIT_FAILURE);
    }
    dup2(0, 1);
    dup2(0, 2);
}

int main()
{
    daemonize();

    openlog("lab1_daemon", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "daemon started");

    for (int i = 1; i <= 12; ++i) {
        sleep(5);
        syslog(LOG_INFO, "tick %d", i);
    }

    syslog(LOG_INFO, "daemon finished");
    closelog();
    return 0;
}