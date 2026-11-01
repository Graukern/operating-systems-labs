#include "test_support.hpp"
#include "daemon.hpp"
#include "pidfile.hpp"

#include <syslog.h>

TEST(daemon_is_singleton) {
    CHECK(&Daemon::instance() == &Daemon::instance());
}

static int launch(const std::string& cwd, const std::string& cfg, const std::string& pidf,
                  const std::string& logf) {
    fflush(nullptr);
    pid_t c = fork();
    if (c == 0) {
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(99);
        Daemon& d = Daemon::instance();
        d.setPidFile(pidf);
        d.setSink([logf](int prio, const std::string& m) {
            FILE* f = fopen(logf.c_str(), "a");
            if (f) {
                fprintf(f, "%d %s\n", prio, m.c_str());
                fclose(f);
            }
        });
        _exit(d.run(cfg));
    }
    int st = 0;
    waitpid(c, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static long waitPid(const std::string& pidf, long different = -1) {
    long pid = -1;
    waitFor(
        [&] {
            pid = pidfile::read(pidf);
            return pid > 1 && pid != different;
        },
        5000);
    return pid;
}

static std::string prio(int p) { return std::to_string(p) + " "; }

TEST(daemon_fails_on_missing_config) {
    TempDir t;
    CHECK_EQ(launch(t.path, "no_such_config", t.path + "/pid", t.path + "/log"), 1);
    CHECK(!exists(t.path + "/pid"));
}

TEST(daemon_full_lifecycle) {
    TempDir t;
    std::string w1 = t.path + "/w1", w2 = t.path + "/w2";
    mkdir(w1.c_str(), 0755);
    mkdir(w2.c_str(), 0755);
    std::string cfg = t.path + "/cfg", pidf = t.path + "/pid", logf = t.path + "/log";
    writeFile(cfg, "dir " + w1 + "\n");

    CHECK_EQ(launch(t.path, "cfg", pidf, logf), 0);
    long pid = waitPid(pidf);
    CHECK(pid > 1);
    CHECK(pidfile::processAlive(static_cast<pid_t>(pid)));
    CHECK(waitFor([&] { return contains(readFile(logf), "watching " + w1); }));
    CHECK(contains(readFile(logf), "daemon started, config: " + cfg));

    writeFile(w1 + "/a.txt", "hello");
    std::string ev = prio(LOG_LOCAL0 | LOG_INFO) + w1 + "/a.txt CREATE";
    CHECK(waitFor([&] { return contains(readFile(logf), ev); }));

    writeFile(cfg, "dir " + w1 + "\ndir " + w2 + "\n");
    sendSig(pid, SIGHUP);
    CHECK(waitFor([&] { return contains(readFile(logf), "watching " + w2); }));
    CHECK(contains(readFile(logf), "SIGHUP: reloading config"));
    writeFile(w2 + "/b.txt", "x");
    CHECK(waitFor([&] { return contains(readFile(logf), w2 + "/b.txt CREATE"); }));

    unlink(cfg.c_str());
    sendSig(pid, SIGHUP);
    CHECK(waitFor([&] { return contains(readFile(logf), "cannot open config " + cfg); }));
    writeFile(w1 + "/c.txt", "x");
    CHECK(waitFor([&] { return contains(readFile(logf), w1 + "/c.txt CREATE"); }));

    sendSig(pid, SIGTERM);
    CHECK(waitFor([&] { return !pidfile::processAlive(static_cast<pid_t>(pid)); }));
    CHECK(contains(readFile(logf), "daemon exiting"));
    CHECK(!exists(pidf));
}

TEST(daemon_second_start_terminates_first) {
    TempDir t;
    std::string w = t.path + "/w";
    mkdir(w.c_str(), 0755);
    std::string cfg = t.path + "/cfg", pidf = t.path + "/pid";
    writeFile(cfg, "dir " + w + "\n");

    CHECK_EQ(launch(t.path, cfg, pidf, t.path + "/log1"), 0);
    long a = waitPid(pidf);
    CHECK(a > 1);
    CHECK(pidfile::processAlive(static_cast<pid_t>(a)));

    CHECK_EQ(launch(t.path, cfg, pidf, t.path + "/log2"), 0);
    long b = waitPid(pidf, a);
    CHECK(b > 1);
    CHECK(b != a);
    CHECK(!pidfile::processAlive(static_cast<pid_t>(a)));  
    CHECK(pidfile::processAlive(static_cast<pid_t>(b)));
    CHECK(contains(readFile(t.path + "/log1"), "daemon exiting"));
    CHECK(waitFor([&] { return contains(readFile(t.path + "/log2"), "previous instance terminated"); }));
    CHECK_EQ(pidfile::read(pidf), b);  

    sendSig(b, SIGTERM);
    CHECK(waitFor([&] { return !pidfile::processAlive(static_cast<pid_t>(b)); }));
    CHECK(!exists(pidf));
}

TEST(daemon_has_no_controlling_terminal_and_new_session) {
    TempDir t;
    std::string cfg = t.path + "/cfg", pidf = t.path + "/pid";
    writeFile(cfg, "dir " + t.path + "\n");
    CHECK_EQ(launch(t.path, cfg, pidf, t.path + "/log"), 0);
    long pid = waitPid(pidf);
    CHECK(pid > 1);
    char buf[256];
    ssize_t n = readlink(("/proc/" + std::to_string(pid) + "/cwd").c_str(), buf, sizeof(buf) - 1);
    CHECK(n > 0);
    if (n > 0) {
        buf[n] = 0;
        CHECK_EQ(std::string(buf), std::string("/"));
    }
    CHECK_EQ(static_cast<long>(getsid(static_cast<pid_t>(pid))), pid);
    sendSig(pid, SIGTERM);
    CHECK(waitFor([&] { return !pidfile::processAlive(static_cast<pid_t>(pid)); }));
}