#include "log.h"

#include <syslog.h>

#include <cstdarg>

namespace dlog {

void open(bool alsoStderr) {
    int options = LOG_PID | LOG_NDELAY;
    if (alsoStderr) {
        options |= LOG_PERROR;
    }
    openlog("diskmon", options, LOG_DAEMON);
}

void close() {
    closelog();
}

void info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsyslog(LOG_DAEMON | LOG_INFO, fmt, args);
    va_end(args);
}

void warning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsyslog(LOG_DAEMON | LOG_WARNING, fmt, args);
    va_end(args);
}

void error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsyslog(LOG_DAEMON | LOG_ERR, fmt, args);
    va_end(args);
}

void event(const std::string& line) {
    syslog(LOG_LOCAL0 | LOG_INFO, "%s", line.c_str());
}

}  // namespace dlog
