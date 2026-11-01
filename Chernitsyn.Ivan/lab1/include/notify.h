#pragma once

#include <sys/types.h>

#include "event.h"

#define NOTIFY_ARGV_MAX 12

int notify_build_argv(const char *terminal, const char *message, const char **argv);
void notify_format(const struct event *ev, time_t at, char *buf, size_t size);
pid_t notify_show(const char *terminal, const struct event *ev, time_t at);
