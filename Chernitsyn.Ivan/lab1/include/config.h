#pragma once

#include <stddef.h>

#include "event.h"

#define DEFAULT_INTERVAL 10
#define TERMINAL_MAX 256

struct config {
    unsigned interval;
    char terminal[TERMINAL_MAX];
    struct event *events;
    size_t count;
    size_t cap;
};

void config_init(struct config *cfg);
int config_load(const char *path, struct config *cfg);
void config_free(struct config *cfg);
