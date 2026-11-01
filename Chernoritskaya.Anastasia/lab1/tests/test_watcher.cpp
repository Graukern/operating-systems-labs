#include "test_support.hpp"
#include <sys/inotify.h>

#include "watcher.hpp"

static std::vector<std::string> collect(Watcher& w, const std::string& until, int ms = 1500) {
    std::vector<std::string> all;
    waitFor(
        [&] {
            for (auto& l : w.readEvents()) all.push_back(l);
            return anyContains(all, until);
        },
        ms);
    return all;
}

TEST(watcher_reports_create_modify_close_delete) {
    TempDir t;
    Watcher w;
    CHECK(w.ok());
    CHECK(w.setDirs({t.path}).empty());
    CHECK_EQ(w.watchCount(), 1u);

    writeFile(t.path + "/a.txt", "x");
    auto ev = collect(w, "a.txt CLOSE_WRITE");
    CHECK(anyContains(ev, t.path + "/a.txt CREATE"));
    CHECK(anyContains(ev, t.path + "/a.txt MODIFY"));
    CHECK(anyContains(ev, t.path + "/a.txt CLOSE_WRITE"));

    unlink((t.path + "/a.txt").c_str());
    ev = collect(w, "a.txt DELETE");
    CHECK(anyContains(ev, t.path + "/a.txt DELETE"));
}

TEST(watcher_reports_read_access) {
    TempDir t;
    writeFile(t.path + "/r.txt", "data");
    Watcher w;
    w.setDirs({t.path});
    readFile(t.path + "/r.txt");
    auto ev = collect(w, "r.txt CLOSE_NOWRITE");
    CHECK(anyContains(ev, "r.txt OPEN"));
    CHECK(anyContains(ev, "r.txt ACCESS"));
    CHECK(anyContains(ev, "r.txt CLOSE_NOWRITE"));
}

TEST(watcher_reports_rename_and_subdir) {
    TempDir t;
    Watcher w;
    w.setDirs({t.path});
    writeFile(t.path + "/old", "1");
    collect(w, "old CLOSE_WRITE");
    CHECK_EQ(rename((t.path + "/old").c_str(), (t.path + "/new").c_str()), 0);
    auto ev = collect(w, "new MOVED_TO");
    CHECK(anyContains(ev, "old MOVED_FROM"));
    CHECK(anyContains(ev, "new MOVED_TO"));
    CHECK_EQ(mkdir((t.path + "/sub").c_str(), 0755), 0);
    ev = collect(w, "sub CREATE (dir)");
    CHECK(anyContains(ev, t.path + "/sub CREATE (dir)"));
}

TEST(watcher_bad_dir_is_reported_and_others_work) {
    TempDir t;
    Watcher w;
    auto failed = w.setDirs({"/nonexistent_diskmon_dir", t.path});
    CHECK_EQ(failed.size(), 1u);
    CHECK_EQ(failed[0].first, std::string("/nonexistent_diskmon_dir"));
    CHECK_EQ(failed[0].second, ENOENT);
    CHECK_EQ(w.watchCount(), 1u);
    writeFile(t.path + "/f", "1");
    CHECK(anyContains(collect(w, "f CREATE"), "f CREATE"));
}

TEST(watcher_setdirs_replaces_old_set) {
    TempDir a, b;
    Watcher w;
    w.setDirs({a.path});
    w.setDirs({b.path});
    CHECK_EQ(w.watchCount(), 1u);
    writeFile(a.path + "/old.txt", "1");
    writeFile(b.path + "/new.txt", "1");
    auto ev = collect(w, "new.txt CLOSE_WRITE");
    CHECK(anyContains(ev, b.path + "/new.txt CREATE"));
    CHECK(!anyContains(ev, a.path));
}

TEST(watcher_no_events_when_idle) {
    TempDir t;
    Watcher w;
    w.setDirs({t.path});
    CHECK(w.readEvents().empty()); 
}