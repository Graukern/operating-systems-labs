#include <cstdio>
#include <cstdlib>
#include <string>

#include "Daemon.h"

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <config>\n", argv[0]);
        return EXIT_FAILURE;
    }

    Daemon& daemon = Daemon::instance();

    std::string error;
    if (!daemon.setup(argv[1], error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    daemon.run();
    return 0;
}