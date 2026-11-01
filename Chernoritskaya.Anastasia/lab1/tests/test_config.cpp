#include "test_support.hpp"
#include "config.hpp"

TEST(config_parses_dirs) {
    std::istringstream in("dir /tmp/a\ndir /var/log\n");
    auto r = config::parse(in);
    CHECK_EQ(r.dirs.size(), 2u);
    CHECK_EQ(r.dirs[0], std::string("/tmp/a"));
    CHECK_EQ(r.dirs[1], std::string("/var/log"));
    CHECK(r.warnings.empty());
}

TEST(config_ignores_comments_and_blank_lines) {
    std::istringstream in("# comment\n\n   \n  # another\ndir /tmp/a\n");
    auto r = config::parse(in);
    CHECK_EQ(r.dirs.size(), 1u);
    CHECK(r.warnings.empty());
}

TEST(config_trims_whitespace_and_crlf) {
    std::istringstream in("  dir \t /tmp/a b  \r\n");
    auto r = config::parse(in);
    CHECK_EQ(r.dirs.size(), 1u);
    CHECK_EQ(r.dirs[0], std::string("/tmp/a b"));
}

TEST(config_strips_trailing_slash_but_keeps_root) {
    std::istringstream in("dir /tmp/a//\ndir /\n");
    auto r = config::parse(in);
    CHECK_EQ(r.dirs.size(), 2u);
    CHECK_EQ(r.dirs[0], std::string("/tmp/a"));
    CHECK_EQ(r.dirs[1], std::string("/"));
}

TEST(config_rejects_relative_path) {
    std::istringstream in("dir relative/path\n");
    auto r = config::parse(in);
    CHECK(r.dirs.empty());
    CHECK_EQ(r.warnings.size(), 1u);
    CHECK(contains(r.warnings[0], "line 1"));
    CHECK(contains(r.warnings[0], "absolute"));
}

TEST(config_warns_on_unknown_directive_and_missing_path) {
    std::istringstream in("foo bar\ndir\ndir /tmp/ok\n");
    auto r = config::parse(in);
    CHECK_EQ(r.dirs.size(), 1u);
    CHECK_EQ(r.warnings.size(), 2u);
    CHECK(contains(r.warnings[0], "unknown directive 'foo'"));
    CHECK(contains(r.warnings[1], "without path"));
}

TEST(config_drops_duplicates) {
    std::istringstream in("dir /tmp/a\ndir /tmp/a/\n");
    auto r = config::parse(in);
    CHECK_EQ(r.dirs.size(), 1u);
    CHECK_EQ(r.warnings.size(), 1u);
}

TEST(config_load_file) {
    TempDir t;
    writeFile(t.path + "/c", "dir /tmp\n");
    config::Result r;
    CHECK(config::loadFile(t.path + "/c", r));
    CHECK_EQ(r.dirs.size(), 1u);
    CHECK(!config::loadFile(t.path + "/missing", r));
}