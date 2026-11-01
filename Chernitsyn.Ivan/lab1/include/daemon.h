#pragma once

#include <limits.h>
#include <signal.h>
#include <time.h>

#include "config.h"

#define PID_FILE "/tmp/reminderd.pid"

class Daemon {
public:
    static Daemon &instance();

    int run(const char *config);

private:
    Daemon();
    Daemon(const Daemon &) = delete;
    Daemon &operator=(const Daemon &) = delete;

    int daemonize();
    void setup_signals();
    void reload();
    void check_events();

    static void on_signal(int sig);

    char config_path[PATH_MAX];
    struct config cfg;
    time_t last_check;

    static volatile sig_atomic_t need_reload;
    static volatile sig_atomic_t need_exit;
};
