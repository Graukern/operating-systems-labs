#include <stdint.h>
#include <criterion/criterion.h>
#include <string.h>

#include "event.h"

static time_t local_time(int year, int mon, int day, int hour, int min, int sec)
{
    struct tm tm;

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

Test(event_parse, simple)
{
    struct event ev;

    cr_assert_eq(event_parse("2026-10-25 18:30 Buy milk", &ev), 0);
    cr_assert_eq(ev.when, local_time(2026, 10, 25, 18, 30, 0));
    cr_assert_eq(ev.repeat, REPEAT_NONE);
    cr_assert_str_eq(ev.text, "Buy milk");
}

Test(event_parse, with_seconds)
{
    struct event ev;

    cr_assert_eq(event_parse("2026-01-02 03:04:05 text", &ev), 0);
    cr_assert_eq(ev.when, local_time(2026, 1, 2, 3, 4, 5));
}

Test(event_parse, flags)
{
    struct event ev;

    cr_assert_eq(event_parse("2026-10-25 10:00 -h hourly", &ev), 0);
    cr_assert_eq(ev.repeat, REPEAT_HOUR);
    cr_assert_str_eq(ev.text, "hourly");

    cr_assert_eq(event_parse("2026-10-25 10:00 -d daily", &ev), 0);
    cr_assert_eq(ev.repeat, REPEAT_DAY);

    cr_assert_eq(event_parse("2026-10-25 10:00   -w   weekly  ", &ev), 0);
    cr_assert_eq(ev.repeat, REPEAT_WEEK);
    cr_assert_str_eq(ev.text, "weekly");
}

Test(event_parse, unknown_flag_is_text)
{
    struct event ev;

    cr_assert_eq(event_parse("2026-10-25 10:00 -x hello", &ev), 0);
    cr_assert_eq(ev.repeat, REPEAT_NONE);
    cr_assert_str_eq(ev.text, "-x hello");

    cr_assert_eq(event_parse("2026-10-25 10:00 -day off", &ev), 0);
    cr_assert_eq(ev.repeat, REPEAT_NONE);
    cr_assert_str_eq(ev.text, "-day off");
}

Test(event_parse, invalid)
{
    struct event ev;

    cr_assert_neq(event_parse("", &ev), 0);
    cr_assert_neq(event_parse("hello world", &ev), 0);
    cr_assert_neq(event_parse("2026-10-25 18:30", &ev), 0);
    cr_assert_neq(event_parse("2026-10-25 18:30 -d", &ev), 0);
    cr_assert_neq(event_parse("2026-10-25 18:30   ", &ev), 0);
    cr_assert_neq(event_parse("2026-13-01 10:00 x", &ev), 0);
    cr_assert_neq(event_parse("2026-02-30 10:00 x", &ev), 0);
    cr_assert_neq(event_parse("2026-10-25 24:00 x", &ev), 0);
    cr_assert_neq(event_parse("2026-10-25 10:60 x", &ev), 0);
    cr_assert_neq(event_parse("2026-10-25 10:00x", &ev), 0);
}

Test(event_parse, long_text_truncated)
{
    struct event ev;
    char line[EVENT_TEXT_MAX * 2];

    strcpy(line, "2026-10-25 10:00 ");
    memset(line + strlen(line), 'a', EVENT_TEXT_MAX + 10);
    line[strlen("2026-10-25 10:00 ") + EVENT_TEXT_MAX + 10] = '\0';

    cr_assert_eq(event_parse(line, &ev), 0);
    cr_assert_eq(strlen(ev.text), EVENT_TEXT_MAX - 1);
}

Test(repeat, periods)
{
    cr_assert_eq(repeat_period(REPEAT_NONE), 0);
    cr_assert_eq(repeat_period(REPEAT_HOUR), 3600);
    cr_assert_eq(repeat_period(REPEAT_DAY), 86400);
    cr_assert_eq(repeat_period(REPEAT_WEEK), 604800);
}

Test(event_due, once)
{
    struct event ev;
    time_t at = 0;

    ev.when = 1000;
    ev.repeat = REPEAT_NONE;

    cr_assert_not(event_due(&ev, 980, 990, &at));
    cr_assert(event_due(&ev, 990, 1000, &at));
    cr_assert_eq(at, 1000);
    cr_assert(event_due(&ev, 999, 1005, &at));
    cr_assert_not(event_due(&ev, 1000, 1010, &at));
    cr_assert_not(event_due(&ev, 5000, 9000, &at));
}

Test(event_due, hourly)
{
    struct event ev;
    time_t at = 0;

    ev.when = 1000;
    ev.repeat = REPEAT_HOUR;

    cr_assert(event_due(&ev, 995, 1005, &at));
    cr_assert_eq(at, 1000);
    cr_assert_not(event_due(&ev, 1005, 1015, &at));
    cr_assert(event_due(&ev, 4595, 4605, &at));
    cr_assert_eq(at, 4600);
    cr_assert(event_due(&ev, 1000 + 3600 * 5 - 1, 1000 + 3600 * 5, &at));
    cr_assert_eq(at, 1000 + 3600 * 5);
    cr_assert_not(event_due(&ev, 4600, 8199, &at));
}

Test(event_due, daily_and_weekly)
{
    struct event ev;
    time_t at = 0;

    ev.when = 100000;
    ev.repeat = REPEAT_DAY;
    cr_assert(event_due(&ev, 100000 + 86400 * 3 - 10, 100000 + 86400 * 3, &at));
    cr_assert_eq(at, 100000 + 86400 * 3);
    cr_assert_not(event_due(&ev, 100000 + 3600, 100000 + 7200, &at));

    ev.repeat = REPEAT_WEEK;
    cr_assert_not(event_due(&ev, 100000 + 86400 * 3 - 10, 100000 + 86400 * 3, &at));
    cr_assert(event_due(&ev, 100000 + 604800 * 2 - 5, 100000 + 604800 * 2 + 5, &at));
    cr_assert_eq(at, 100000 + 604800 * 2);
}

Test(event_due, future)
{
    struct event ev;

    ev.when = 1000;
    ev.repeat = REPEAT_DAY;
    cr_assert_not(event_due(&ev, 900, 999, NULL));
}
