// Observe calls at the syslog API boundary AND forward them to the real logger.
// No filesystem-monitoring code is replaced by this test helper.
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <string>
#include <syslog.h>
#include <unistd.h>

namespace {
int facility = LOG_USER;
}

extern "C" void openlog(const char* ident, int options, int selectedFacility) {
    facility = selectedFacility;
    const auto original = reinterpret_cast<void (*)(const char*, int, int)>(dlsym(RTLD_NEXT, "openlog"));
    original(ident, options, selectedFacility);
}

namespace {
void capture(int priority, const char* format, va_list arguments) {
    const int saved = errno;
    char message[16384];
    errno = saved;
    vsnprintf(message, sizeof(message), format, arguments);
    const int selectedFacility = (priority & LOG_FACMASK) != 0 ? (priority & LOG_FACMASK) : facility;
    const std::string line = "facility=" + std::to_string(selectedFacility >> 3) +
        " priority=" + std::to_string(LOG_PRI(priority)) + " pid=" +
        std::to_string(getpid()) + " " + message + "\n";
    if (const char* path = std::getenv("DISKMON_TEST_LOG")) {
        const int file = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (file >= 0) {
            const auto written = write(file, line.data(), line.size());
            (void)written;
            close(file);
        }
    }
    const auto original = reinterpret_cast<void (*)(int, const char*, ...)>(dlsym(RTLD_NEXT, "syslog"));
    original(priority, "%s", message);
    errno = saved;
}
} // namespace

extern "C" void syslog(int priority, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    capture(priority, format, arguments);
    va_end(arguments);
}

// Ubuntu's fortified libc redirects optimized syslog calls to this entry point.
extern "C" void __syslog_chk(int priority, int, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    capture(priority, format, arguments);
    va_end(arguments);
}
