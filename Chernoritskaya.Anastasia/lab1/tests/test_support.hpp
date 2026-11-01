#pragma once
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    void (*fn)();
};
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};
inline int g_fail = 0;

#define TEST(name)                                \
    static void name();                           \
    static Registrar reg_##name(#name, name);     \
    static void name()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ++g_fail;                                                               \
            std::cout << "    CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        auto va = (a);                                                              \
        auto vb = (b);                                                              \
        if (!(va == vb)) {                                                          \
            ++g_fail;                                                               \
            std::ostringstream os;                                                  \
            os << "    CHECK_EQ failed: " #a " == " #b "\n      left:  " << va       \
               << "\n      right: " << vb << "  (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::cout << os.str();                                                  \
        }                                                                           \
    } while (0)

struct TempDir {
    std::string path;
    TempDir() {
        char t[] = "/tmp/diskmon_test_XXXXXX";
        if (!mkdtemp(t)) {
            std::perror("mkdtemp");
            std::exit(2);
        }
        path = t;
    }
    ~TempDir() {
        int rc = std::system(("rm -rf '" + path + "'").c_str());
        (void)rc;
    }
};

inline void writeFile(const std::string& p, const std::string& s) {
    std::ofstream o(p, std::ios::trunc);
    o << s;
}

inline std::string readFile(const std::string& p) {
    std::ifstream i(p);
    std::ostringstream os;
    os << i.rdbuf();
    return os.str();
}

inline bool exists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }

inline bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

inline bool anyContains(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& s : v)
        if (contains(s, needle)) return true;
    return false;
}

inline bool waitFor(const std::function<bool()>& pred, int timeoutMs = 3000) {
    for (int t = 0; t < timeoutMs; t += 20) {
        if (pred()) return true;
        usleep(20000);
    }
    return pred();
}

inline void sendSig(long pid, int sig) {
    if (pid > 1) kill(static_cast<pid_t>(pid), sig);
}