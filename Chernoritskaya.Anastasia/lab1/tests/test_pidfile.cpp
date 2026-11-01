#include "test_support.hpp"
#include "pidfile.hpp"

TEST(pidfile_read_missing_and_garbage) {
    TempDir t;
    CHECK_EQ(pidfile::read(t.path + "/none"), -1L);
    writeFile(t.path + "/bad", "abc\n");
    CHECK_EQ(pidfile::read(t.path + "/bad"), -1L);
    writeFile(t.path + "/neg", "-5\n");
    CHECK_EQ(pidfile::read(t.path + "/neg"), -1L);
}

TEST(pidfile_write_read_roundtrip) {
    TempDir t;
    CHECK(pidfile::write(t.path + "/p", 4242));
    CHECK_EQ(pidfile::read(t.path + "/p"), 4242L);
    CHECK(!pidfile::write(t.path + "/no/such/dir/p", 1));
}

static void makeFakeProc(const std::string& root, int pid, char state) {
    std::string d = root + "/" + std::to_string(pid);
    int rc = std::system(("mkdir -p '" + d + "'").c_str());
    (void)rc;
    writeFile(d + "/stat", std::to_string(pid) + " (my (weird) name) " + state + " 1 1 1\n");
}

TEST(pidfile_process_alive_real_proc) {
    CHECK(pidfile::processAlive(getpid()));
    CHECK(!pidfile::processAlive(0));
    CHECK(!pidfile::processAlive(-3));
    CHECK(!pidfile::processAlive(2000000000));
}

TEST(pidfile_process_alive_fake_proc_states) {
    TempDir t;
    makeFakeProc(t.path, 100, 'S');
    makeFakeProc(t.path, 101, 'R');
    makeFakeProc(t.path, 102, 'Z'); 
    CHECK(pidfile::processAlive(100, t.path));
    CHECK(pidfile::processAlive(101, t.path));
    CHECK(!pidfile::processAlive(102, t.path));
    CHECK(!pidfile::processAlive(103, t.path));
}

TEST(pidfile_terminate_previous_none_cases) {
    TempDir t;
    std::string pf = t.path + "/p";
    CHECK(pidfile::terminatePrevious(pf, getpid()) == pidfile::Prev::None);  
    pidfile::write(pf, getpid());
    CHECK(pidfile::terminatePrevious(pf, getpid()) == pidfile::Prev::None);  
    pidfile::write(pf, 777);
    makeFakeProc(t.path, 778, 'S');  
    CHECK(pidfile::terminatePrevious(pf, getpid(), t.path, 200) == pidfile::Prev::None);
}

TEST(pidfile_terminate_previous_kills_live_process) {
    TempDir t;
    std::string pf = t.path + "/p";
    fflush(nullptr);
    pid_t child = fork();
    if (child == 0) {
        for (;;) pause();
    }
    CHECK(child > 0);
    pidfile::write(pf, child);
    auto res = pidfile::terminatePrevious(pf, getpid(), "/proc", 3000);
    CHECK(res == pidfile::Prev::Terminated);
    int st = 0;
    waitpid(child, &st, 0);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
}

TEST(pidfile_remove_if_owner) {
    TempDir t;
    std::string pf = t.path + "/p";
    pidfile::write(pf, 555);
    CHECK(!pidfile::removeIfOwner(pf, 556));  
    CHECK(exists(pf));
    CHECK(pidfile::removeIfOwner(pf, 555));
    CHECK(!exists(pf));
    CHECK(!pidfile::removeIfOwner(pf, 555));  
}