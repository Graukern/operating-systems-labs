#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <climits>
#include <string>

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

static volatile std::sig_atomic_t g_terminate = 0;
static volatile std::sig_atomic_t g_reload = 0;

static void handleSignal(int sig)
{
    if (sig == SIGTERM) {
        g_terminate = 1;
    } else if (sig == SIGHUP) {
        g_reload = 1;
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <config>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char absPath[PATH_MAX];
    if (realpath(argv[1], absPath) == nullptr) {
        std::perror("realpath");
        return EXIT_FAILURE;
    }
    const std::string configPath = absPath;

    daemonize();

    openlog("lab1_daemon", LOG_PID, LOG_DAEMON);

    struct sigaction sa {};
    sa.sa_handler = handleSignal;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    syslog(LOG_INFO, "daemon started, config: %s", configPath.c_str());

    while (!g_terminate) {
        if (g_reload) {
            g_reload = 0;
            syslog(LOG_INFO, "SIGHUP received: reload config (stub)");
        }
        syslog(LOG_INFO, "working...");
        sleep(5);
    }

    syslog(LOG_INFO, "SIGTERM received, daemon exiting");
    closelog();
    return 0;
}