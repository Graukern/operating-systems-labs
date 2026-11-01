#include "daemon.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "notify.h"
#include "pidfile.h"

volatile sig_atomic_t Daemon::need_reload = 0;
volatile sig_atomic_t Daemon::need_exit = 0;

Daemon &Daemon::instance()
{
    static Daemon d;
    return d;
}

Daemon::Daemon() : last_check(0)
{
    config_path[0] = '\0';
    config_init(&cfg);
}

void Daemon::on_signal(int sig)
{
    if (sig == SIGHUP)
        need_reload = 1;
    else if (sig == SIGTERM)
        need_exit = 1;
}

void Daemon::setup_signals()
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
}

int Daemon::daemonize()
{
    pid_t pid;
    long maxfd;
    int fd;

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        exit(0);

    if (setsid() < 0)
        return -1;

    umask(0);
    if (chdir("/") != 0)
        return -1;

    maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 4096)
        maxfd = 4096;
    for (fd = 0; fd < maxfd; fd++)
        close(fd);

    fd = open("/dev/null", O_RDWR);
    if (fd != 0)
        return -1;
    if (dup(0) != 1 || dup(0) != 2)
        return -1;

    return 0;
}

void Daemon::reload()
{
    struct config fresh;

    if (config_load(config_path, &fresh) != 0) {
        syslog(LOG_ERR, "cannot read config %s: %s", config_path, strerror(errno));
        return;
    }

    config_free(&cfg);
    cfg = fresh;
    syslog(LOG_INFO, "config loaded: %zu events, interval %us", cfg.count, cfg.interval);
}

void Daemon::check_events()
{
    time_t now = time(NULL);
    time_t at;
    size_t i;

    for (i = 0; i < cfg.count; i++) {
        if (!event_due(&cfg.events[i], last_check, now, &at))
            continue;
        syslog(LOG_INFO, "reminder: %s", cfg.events[i].text);
        if (notify_show(cfg.terminal, &cfg.events[i], at) < 0)
            syslog(LOG_ERR, "fork failed: %s", strerror(errno));
    }

    last_check = now;
}

int Daemon::run(const char *config)
{
    if (!realpath(config, config_path)) {
        fprintf(stderr, "reminderd: %s: %s\n", config, strerror(errno));
        return 1;
    }
    if (access(config_path, R_OK) != 0) {
        fprintf(stderr, "reminderd: %s: %s\n", config_path, strerror(errno));
        return 1;
    }

    openlog("reminderd", LOG_PID, LOG_DAEMON);

    if (stop_running(PID_FILE) > 0)
        syslog(LOG_INFO, "previous instance stopped");
    closelog();

    int rc = daemonize();
    int err = errno;

    openlog("reminderd", LOG_PID, LOG_DAEMON);
    if (rc != 0) {
        syslog(LOG_ERR, "daemonize failed: %s", strerror(err));
        closelog();
        return 1;
    }

    setup_signals();

    if (pidfile_write(PID_FILE, getpid()) != 0) {
        syslog(LOG_ERR, "cannot write %s: %s", PID_FILE, strerror(errno));
        closelog();
        return 1;
    }

    syslog(LOG_INFO, "started, config %s", config_path);

    reload();
    last_check = time(NULL);

    while (!need_exit) {
        if (need_reload) {
            need_reload = 0;
            syslog(LOG_INFO, "SIGHUP received, reloading config");
            reload();
        }
        check_events();
        if (!need_exit && !need_reload)
            sleep(cfg.interval);
    }

    syslog(LOG_INFO, "SIGTERM received, exiting");
    pidfile_remove(PID_FILE);
    config_free(&cfg);
    closelog();
    return 0;
}
