#include <stdint.h>
#include <criterion/criterion.h>
#include <string.h>

#include "notify.h"

Test(notify, xterm_argv)
{
    const char *argv[NOTIFY_ARGV_MAX];
    int n = notify_build_argv("xterm", "hi", argv);

    cr_assert_eq(n, 9);
    cr_assert_str_eq(argv[0], "xterm");
    cr_assert_str_eq(argv[3], "-e");
    cr_assert_str_eq(argv[4], "sh");
    cr_assert_str_eq(argv[5], "-c");
    cr_assert_str_eq(argv[8], "hi");
    cr_assert_null(argv[9]);
}

Test(notify, gnome_terminal_argv)
{
    const char *argv[NOTIFY_ARGV_MAX];
    int n = notify_build_argv("/usr/bin/gnome-terminal", "hi", argv);

    cr_assert_eq(n, 8);
    cr_assert_str_eq(argv[0], "/usr/bin/gnome-terminal");
    cr_assert_str_eq(argv[2], "--");
    cr_assert_str_eq(argv[3], "sh");
}

Test(notify, kitty_argv)
{
    const char *argv[NOTIFY_ARGV_MAX];
    int n = notify_build_argv("kitty", "hi", argv);

    cr_assert_eq(n, 8);
    cr_assert_str_eq(argv[3], "sh");
    cr_assert_str_eq(argv[7], "hi");
}

Test(notify, generic_argv)
{
    const char *argv[NOTIFY_ARGV_MAX];
    int n = notify_build_argv("konsole", "hi", argv);

    cr_assert_eq(n, 7);
    cr_assert_str_eq(argv[1], "-e");
    cr_assert_str_eq(argv[6], "hi");
    cr_assert_null(argv[7]);
}

Test(notify, message_is_separate_argument)
{
    const char *argv[NOTIFY_ARGV_MAX];
    const char *msg = "it's \"quoted\" $(rm -rf /) `x`";
    int n = notify_build_argv("xterm", msg, argv);

    cr_assert_eq(argv[n - 1], msg);
}

Test(notify, format)
{
    struct event ev;
    struct tm tm;
    char buf[EVENT_TEXT_MAX + 64];

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = 25;
    tm.tm_hour = 7;
    tm.tm_min = 5;
    tm.tm_isdst = -1;

    strcpy(ev.text, "Wake up");
    notify_format(&ev, mktime(&tm), buf, sizeof(buf));
    cr_assert_str_eq(buf, "Reminder [2026-10-25 07:05]\n\nWake up");
}
