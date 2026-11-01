#include <stdio.h>

#include "daemon.h"

int main(int argc, char **argv)
{
    const char *config = "reminder.conf";

    if (argc > 2) {
        fprintf(stderr, "usage: %s [config]\n", argv[0]);
        return 1;
    }
    if (argc == 2)
        config = argv[1];

    return Daemon::instance().run(config);
}
