#include <stdint.h>
#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

static char path[64];

static void write_config(const char *text)
{
    FILE *f;
    int fd;

    strcpy(path, "/tmp/reminder_test_XXXXXX");
    fd = mkstemp(path);
    cr_assert_geq(fd, 0);
    f = fdopen(fd, "w");
    fputs(text, f);
    fclose(f);
}

static void cleanup(void)
{
    if (path[0])
        unlink(path);
    path[0] = '\0';
}

TestSuite(config, .fini = cleanup);

Test(config, missing_file)
{
    struct config cfg;

    config_init(&cfg);
    cr_assert_eq(config_load("/nonexistent/reminder.conf", &cfg), -1);
}

Test(config, defaults)
{
    struct config cfg;

    write_config("");
    cr_assert_eq(config_load(path, &cfg), 0);
    cr_assert_eq(cfg.interval, DEFAULT_INTERVAL);
    cr_assert_str_empty(cfg.terminal);
    cr_assert_eq(cfg.count, 0);
    config_free(&cfg);
}

Test(config, full)
{
    struct config cfg;

    write_config(
        "# comment\n"
        "\n"
        "interval 5\n"
        "terminal konsole\n"
        "2026-10-25 18:00 first\n"
        "   # indented comment\n"
        "2026-10-26 09:30 -d second one\n"
        "2026-10-27 12:00:15 -w third");

    cr_assert_eq(config_load(path, &cfg), 0);
    cr_assert_eq(cfg.interval, 5);
    cr_assert_str_eq(cfg.terminal, "konsole");
    cr_assert_eq(cfg.count, 3);
    cr_assert_str_eq(cfg.events[0].text, "first");
    cr_assert_eq(cfg.events[1].repeat, REPEAT_DAY);
    cr_assert_str_eq(cfg.events[1].text, "second one");
    cr_assert_eq(cfg.events[2].repeat, REPEAT_WEEK);
    cr_assert_str_eq(cfg.events[2].text, "third");
    config_free(&cfg);
    cr_assert_null(cfg.events);
}

Test(config, bad_lines_skipped)
{
    struct config cfg;

    write_config(
        "interval 0\n"
        "interval abc\n"
        "terminal\n"
        "garbage here\n"
        "2026-99-99 10:00 bad date\n"
        "2026-10-25 10:00 good\n");

    cr_assert_eq(config_load(path, &cfg), 0);
    cr_assert_eq(cfg.interval, DEFAULT_INTERVAL);
    cr_assert_str_empty(cfg.terminal);
    cr_assert_eq(cfg.count, 1);
    cr_assert_str_eq(cfg.events[0].text, "good");
    config_free(&cfg);
}

Test(config, long_terminal_path)
{
    struct config cfg;
    char buf[2 * TERMINAL_MAX];

    strcpy(buf, "terminal /");
    memset(buf + 10, 'x', TERMINAL_MAX);
    strcpy(buf + 10 + TERMINAL_MAX, "\nterminal /usr/local/bin/my-terminal-emulator\n");
    write_config(buf);

    cr_assert_eq(config_load(path, &cfg), 0);
    cr_assert_str_eq(cfg.terminal, "/usr/local/bin/my-terminal-emulator");
    config_free(&cfg);
}

Test(config, many_events)
{
    struct config cfg;
    char *buf = (char *)malloc(100 * 64);
    char *p = buf;
    int i;

    for (i = 0; i < 100; i++)
        p += sprintf(p, "2026-10-25 10:%02d event %d\n", i % 60, i);
    write_config(buf);
    free(buf);

    cr_assert_eq(config_load(path, &cfg), 0);
    cr_assert_eq(cfg.count, 100);
    cr_assert_str_eq(cfg.events[99].text, "event 99");
    config_free(&cfg);
}
