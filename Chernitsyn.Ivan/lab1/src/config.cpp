#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

void config_init(struct config *cfg)
{
    cfg->interval = DEFAULT_INTERVAL;
    cfg->terminal[0] = '\0';
    cfg->events = NULL;
    cfg->count = 0;
    cfg->cap = 0;
}

void config_free(struct config *cfg)
{
    free(cfg->events);
    config_init(cfg);
}

static int add_event(struct config *cfg, const struct event *ev)
{
    if (cfg->count == cfg->cap) {
        size_t cap = cfg->cap ? cfg->cap * 2 : 8;
        struct event *p = (struct event *)realloc(cfg->events, cap * sizeof(*p));
        if (!p)
            return -1;
        cfg->events = p;
        cfg->cap = cap;
    }
    cfg->events[cfg->count++] = *ev;
    return 0;
}

static int starts_with_word(const char *s, const char *word)
{
    size_t n = strlen(word);
    return strncmp(s, word, n) == 0 && isspace((unsigned char)s[n]);
}

static int parse_line(struct config *cfg, char *line)
{
    struct event ev;

    if (starts_with_word(line, "interval")) {
        unsigned v;
        if (sscanf(line + 8, "%u", &v) != 1 || v == 0)
            return -1;
        cfg->interval = v;
        return 0;
    }

    if (starts_with_word(line, "terminal")) {
        const char *p = line + 8;
        size_t len;

        while (isspace((unsigned char)*p))
            p++;
        len = strcspn(p, " \t\r\n");
        if (len == 0 || len >= sizeof(cfg->terminal))
            return -1;
        memcpy(cfg->terminal, p, len);
        cfg->terminal[len] = '\0';
        return 0;
    }

    if (event_parse(line, &ev) != 0)
        return -1;
    return add_event(cfg, &ev);
}

int config_load(const char *path, struct config *cfg)
{
    FILE *f;
    char line[1024];
    int lineno = 0;

    f = fopen(path, "r");
    if (!f)
        return -1;

    config_init(cfg);

    while (fgets(line, sizeof(line), f)) {
        char *p = line;

        lineno++;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        line[strcspn(line, "\n")] = '\0';
        if (parse_line(cfg, p) != 0)
            syslog(LOG_WARNING, "%s:%d: bad line skipped", path, lineno);
    }

    fclose(f);
    return 0;
}
