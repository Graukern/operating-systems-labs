#include "test_support.hpp"
#include "events.hpp"

#include <sys/inotify.h>

TEST(events_names_single_and_combined) {
    CHECK_EQ(events::names(IN_CREATE), std::string("CREATE"));
    CHECK_EQ(events::names(IN_OPEN | IN_ACCESS), std::string("ACCESS|OPEN"));
    CHECK_EQ(events::names(IN_ISDIR), std::string("UNKNOWN"));
    CHECK_EQ(events::names(0), std::string("UNKNOWN"));
}

TEST(events_format) {
    CHECK_EQ(events::format("/tmp/w", "a.txt", IN_MODIFY), std::string("/tmp/w/a.txt MODIFY"));
    CHECK_EQ(events::format("/tmp/w", "sub", IN_CREATE | IN_ISDIR),
             std::string("/tmp/w/sub CREATE (dir)"));
    CHECK_EQ(events::format("/tmp/w", "", IN_OPEN | IN_ISDIR), std::string("/tmp/w OPEN (dir)"));
}