#include "pidfile.hpp"

#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <fstream>
#include <iterator>

namespace pidfile {

long read(const std::string& path) {
    std::ifstream in(path);
    long v = 0;
    if (!(in >> v) || v <= 0) return -1;
    return v;
}

bool write(const std::string& path, pid_t pid) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << pid << std::endl;
    return static_cast<bool>(out);
}

bool processAlive(pid_t pid, const std::string& procRoot) {
    if (pid <= 0) return false;
    std::ifstream in(procRoot + "/" + std::to_string(pid) + "/stat");
    if (!in) return false;
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t p = s.rfind(')');
    if (p == std::string::npos || p + 2 >= s.size()) return false;
    char state = s[p + 2];
    return state != 'Z' && state != 'X';
}

Prev terminatePrevious(const std::string& path, pid_t self, const std::string& procRoot, int timeoutMs) {
    long p = read(path);
    if (p <= 0 || p == self) return Prev::None;
    pid_t pid = static_cast<pid_t>(p);
    if (!processAlive(pid, procRoot)) return Prev::None;
    if (kill(pid, SIGTERM) != 0) return errno == ESRCH ? Prev::None : Prev::Failed;
    for (int waited = 0; waited < timeoutMs; waited += 50) {
        if (!processAlive(pid, procRoot)) return Prev::Terminated;
        usleep(50000);
    }
    return processAlive(pid, procRoot) ? Prev::Failed : Prev::Terminated;
}

bool removeIfOwner(const std::string& path, pid_t self) {
    if (read(path) != self) return false;
    return unlink(path.c_str()) == 0;
}

}  