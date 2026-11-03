// LD_PRELOAD for tests: writes syslog() messages with their facility to $DISKMON_SYSLOG_FILE.
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace {

const char* g_ident = "?";
int g_options = 0;
int g_facility = LOG_USER;

const char* facilityName(int facility) {
    switch (facility) {
        case LOG_DAEMON: return "daemon";
        case LOG_LOCAL0: return "local0";
        case LOG_USER: return "user";
        default: return "other";
    }
}

const char* levelName(int level) {
    static const char* names[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};
    return names[level & LOG_PRIMASK];
}

void capture(int priority, const char* fmt, va_list args) {
    char text[4096];
    std::vsnprintf(text, sizeof(text), fmt, args);
    int facility = priority & LOG_FACMASK;
    if (facility == 0) facility = g_facility;
    char line[4352];
    int n = std::snprintf(line, sizeof(line), "%s.%s %s[%d]: %s\n", facilityName(facility),
                          levelName(priority), g_ident, getpid(), text);
    if (n > static_cast<int>(sizeof(line)) - 1) n = sizeof(line) - 1;
    if (const char* path = std::getenv("DISKMON_SYSLOG_FILE")) {
        int fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
        if (fd >= 0) {
            if (write(fd, line, n) < 0) {}
            close(fd);
        }
    }
    if (g_options & LOG_PERROR) {
        if (write(STDERR_FILENO, line, n) < 0) {}
    }
}

}  // namespace

extern "C" {

void openlog(const char* ident, int options, int facility) {
    g_ident = ident ? ident : "?";
    g_options = options;
    g_facility = facility;
}

void closelog(void) {}

void vsyslog(int priority, const char* fmt, va_list args) {
    capture(priority, fmt, args);
}

void syslog(int priority, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    capture(priority, fmt, args);
    va_end(args);
}

// With -O2 glibc headers turn syslog/vsyslog into these fortified variants.
void __vsyslog_chk(int priority, int, const char* fmt, va_list args) {
    capture(priority, fmt, args);
}

void __syslog_chk(int priority, int, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    capture(priority, fmt, args);
    va_end(args);
}

}  // extern "C"
