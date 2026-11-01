#include <iostream>

#include "daemon.hpp"

int main(int argc, char* argv[]) {
    const char* cfg = argc > 1 ? argv[1] : "diskmon.conf";
    Daemon& d = Daemon::instance();
    if (argc > 2) d.setPidFile(argv[2]);
    return d.run(cfg);
}