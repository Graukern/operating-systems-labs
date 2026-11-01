#include "notify.h"

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

static const char *script = "printf '%s\\n\\n' \"$1\"; printf 'Press Enter to close...'; read _";

static const char *fallback[] = {
    "x-terminal-emulator",
    "xterm",
    "konsole",
    "gnome-terminal",
    "kitty",
    NULL
};

static const char *base_name(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

int notify_build_argv(const char *terminal, const char *message, const char **argv)
{
    const char *name = base_name(terminal);
    int n = 0;

    argv[n++] = terminal;

    if (strcmp(name, "gnome-terminal") == 0) {
        argv[n++] = "--title=Reminder";
        argv[n++] = "--";
    } else if (strcmp(name, "kitty") == 0) {
        argv[n++] = "--title";
        argv[n++] = "Reminder";
    } else if (strcmp(name, "xterm") == 0) {
        argv[n++] = "-T";
        argv[n++] = "Reminder";
        argv[n++] = "-e";
    } else {
        argv[n++] = "-e";
    }

    argv[n++] = "sh";
    argv[n++] = "-c";
    argv[n++] = script;
    argv[n++] = "reminder";
    argv[n++] = message;
    argv[n] = NULL;
    return n;
}

void notify_format(const struct event *ev, time_t at, char *buf, size_t size)
{
    char stamp[32];
    struct tm tm;

    localtime_r(&at, &tm);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &tm);
    snprintf(buf, size, "Reminder [%s]\n\n%s", stamp, ev->text);
}

static void try_exec(const char *terminal, const char *message)
{
    const char *argv[NOTIFY_ARGV_MAX];

    notify_build_argv(terminal, message, argv);
    execvp(argv[0], (char *const *)argv);
}

pid_t notify_show(const char *terminal, const struct event *ev, time_t at)
{
    char message[EVENT_TEXT_MAX + 64];
    pid_t pid;
    int i;

    notify_format(ev, at, message, sizeof(message));

    pid = fork();
    if (pid != 0)
        return pid;

    setsid();
    if (terminal && terminal[0])
        try_exec(terminal, message);
    for (i = 0; fallback[i]; i++)
        try_exec(fallback[i], message);

    syslog(LOG_ERR, "no terminal emulator found for \"%s\"", ev->text);
    _exit(127);
}
