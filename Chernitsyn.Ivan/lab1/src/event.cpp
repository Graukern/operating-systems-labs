#include "event.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *skip_spaces(const char *p)
{
    while (*p && isspace((unsigned char)*p))
        p++;
    return p;
}

static enum repeat_mode parse_flag(const char **pp)
{
    const char *p = *pp;
    enum repeat_mode mode;

    if (p[0] != '-')
        return REPEAT_NONE;

    switch (p[1]) {
    case 'h':
        mode = REPEAT_HOUR;
        break;
    case 'd':
        mode = REPEAT_DAY;
        break;
    case 'w':
        mode = REPEAT_WEEK;
        break;
    default:
        return REPEAT_NONE;
    }

    if (p[2] != '\0' && !isspace((unsigned char)p[2]))
        return REPEAT_NONE;

    *pp = skip_spaces(p + 2);
    return mode;
}

int event_parse(const char *line, struct event *ev)
{
    int year, mon, day, hour, min, sec = 0;
    int n = 0;
    struct tm tm;
    const char *p;
    size_t len;

    line = skip_spaces(line);
    if (sscanf(line, "%4d-%2d-%2d %2d:%2d%n", &year, &mon, &day, &hour, &min, &n) != 5)
        return -1;

    p = line + n;
    if (*p == ':') {
        int m = 0;
        if (sscanf(p, ":%2d%n", &sec, &m) != 1)
            return -1;
        p += m;
    }

    if (!isspace((unsigned char)*p))
        return -1;

    if (mon < 1 || mon > 12 || day < 1 || day > 31)
        return -1;
    if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59)
        return -1;

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;

    ev->when = mktime(&tm);
    if (ev->when == (time_t)-1)
        return -1;
    if (tm.tm_mday != day || tm.tm_mon != mon - 1)
        return -1;

    p = skip_spaces(p);
    ev->repeat = parse_flag(&p);

    len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1]))
        len--;
    if (len == 0)
        return -1;
    if (len >= sizeof(ev->text))
        len = sizeof(ev->text) - 1;

    memcpy(ev->text, p, len);
    ev->text[len] = '\0';
    return 0;
}

long repeat_period(enum repeat_mode mode)
{
    switch (mode) {
    case REPEAT_HOUR:
        return 60L * 60;
    case REPEAT_DAY:
        return 24L * 60 * 60;
    case REPEAT_WEEK:
        return 7L * 24 * 60 * 60;
    default:
        return 0;
    }
}

int event_due(const struct event *ev, time_t from, time_t to, time_t *fire_at)
{
    long period = repeat_period(ev->repeat);
    time_t t = ev->when;

    if (t > to)
        return 0;

    if (t <= from) {
        if (period == 0)
            return 0;
        t += ((from - t) / period + 1) * period;
        if (t > to)
            return 0;
    }

    if (fire_at)
        *fire_at = t;
    return 1;
}
