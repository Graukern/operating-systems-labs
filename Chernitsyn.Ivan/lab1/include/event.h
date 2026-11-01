#pragma once

#include <time.h>

#define EVENT_TEXT_MAX 512

enum repeat_mode {
    REPEAT_NONE,
    REPEAT_HOUR,
    REPEAT_DAY,
    REPEAT_WEEK
};

struct event {
    time_t when;
    enum repeat_mode repeat;
    char text[EVENT_TEXT_MAX];
};

int event_parse(const char *line, struct event *ev);
long repeat_period(enum repeat_mode mode);
int event_due(const struct event *ev, time_t from, time_t to, time_t *fire_at);
