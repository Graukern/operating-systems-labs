#pragma once

#include <sys/types.h>

pid_t pidfile_read(const char *path);
int pidfile_write(const char *path, pid_t pid);
void pidfile_remove(const char *path);
int process_alive(pid_t pid);
int stop_running(const char *path);
