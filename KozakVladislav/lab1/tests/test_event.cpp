#include "Event.h"
#include <iostream>
#include <string>
#include <ctime>

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT(cond) do { \
    if (cond) { ++g_pass; } \
    else { \
        ++g_fail; \
        std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
    } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) { ++g_pass; } \
    else { \
        ++g_fail; \
        std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ \
                  << ": " #a " == " #b " (" << _a << " vs " << _b << ")\n"; \
    } \
} while (0)

static void testParseValid() {
    std::cout << "testParseValid\n";
    Event ev;
    EXPECT(Event::parse("2025-01-01 12:00:00 -h Hourly", ev));
    EXPECT_EQ(ev.flag(), 'h');
    EXPECT_EQ(ev.text(), std::string("Hourly"));
    EXPECT(Event::parse("2025-01-01 12:00:00 -d Daily", ev));
    EXPECT_EQ(ev.flag(), 'd');
    EXPECT(Event::parse("2025-01-01 12:00:00 -w Weekly", ev));
    EXPECT_EQ(ev.flag(), 'w');
    EXPECT(Event::parse("2025-01-01 12:00:00 One-time", ev));
    EXPECT_EQ(ev.flag(), '\0');
    EXPECT_EQ(ev.text(), std::string("One-time"));
    EXPECT(Event::parse("2025-01-01 12:00:00 -d Buy milk and bread", ev));
    EXPECT_EQ(ev.text(), std::string("Buy milk and bread"));
    EXPECT(Event::parse("2025-01-01 12:00:00 -h    Spaced", ev));
    EXPECT_EQ(ev.text(), std::string("Spaced"));
    EXPECT(Event::parse("2025-01-01 12:00:00 3 apples", ev));
    EXPECT_EQ(ev.flag(), '\0');
    EXPECT_EQ(ev.text(), std::string("3 apples"));
}

static void testParseInvalid() {
    std::cout << "testParseInvalid\n";
    Event ev;
    EXPECT(!Event::parse("", ev));
    EXPECT(!Event::parse("2025-01-01", ev));
    EXPECT(!Event::parse("2025-01-01 12:00:00", ev));
    EXPECT(!Event::parse("2025-13-01 12:00:00 -h Bad month", ev));
    EXPECT(!Event::parse("2025-01-32 12:00:00 -h Bad day", ev));
    EXPECT(!Event::parse("2025-02-30 12:00:00 -h Feb 30", ev));
    EXPECT(!Event::parse("2025-01-01 25:00:00 -h Bad hour", ev));
    EXPECT(!Event::parse("2025-01-01 12:60:00 -h Bad minute", ev));
    EXPECT(!Event::parse("2025-01-01 12:00:00 -x Unknown", ev));
    EXPECT(!Event::parse("2025-01-01 12:00:00 - Bad", ev));
}

static void testNextOccurrence() {
    std::cout << "testNextOccurrence\n";
    const std::time_t base = 1000000000;
    {
        Event ev(base + 1000, 'h', "x");
        EXPECT_EQ(ev.nextOccurrenceAfter(base), base + 1000);
    }
    {
        Event ev(base, '\0', "x");
        EXPECT_EQ(ev.nextOccurrenceAfter(base), static_cast<std::time_t>(0));
    }
    {
        Event ev(base, 'h', "x");
        EXPECT_EQ(ev.nextOccurrenceAfter(base),          base + 3600);
        EXPECT_EQ(ev.nextOccurrenceAfter(base + 1),      base + 3600);
        EXPECT_EQ(ev.nextOccurrenceAfter(base + 3599),   base + 3600);
        EXPECT_EQ(ev.nextOccurrenceAfter(base + 3600),   base + 7200);
        EXPECT_EQ(ev.nextOccurrenceAfter(base + 10000),  base + 10800);
    }
    {
        Event ev(base, 'd', "x");
        EXPECT_EQ(ev.nextOccurrenceAfter(base),         base + 86400);
        EXPECT_EQ(ev.nextOccurrenceAfter(base + 86400), base + 172800);
    }
    {
        Event ev(base, 'w', "x");
        EXPECT_EQ(ev.nextOccurrenceAfter(base),           base + 604800);
        EXPECT_EQ(ev.nextOccurrenceAfter(base + 604800),  base + 1209600);
    }
}

static void testKey() {
    std::cout << "testKey\n";
    Event a(100, 'h', "text");
    Event b(100, 'h', "text");
    Event c(101, 'h', "text");
    Event d(100, 'd', "text");
    Event e(100, 'h', "other");
    EXPECT_EQ(a.key(), b.key());
    EXPECT(a.key() != c.key());
    EXPECT(a.key() != d.key());
    EXPECT(a.key() != e.key());
}

int main() {
    testParseValid();
    testParseInvalid();
    testNextOccurrence();
    testKey();
    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}