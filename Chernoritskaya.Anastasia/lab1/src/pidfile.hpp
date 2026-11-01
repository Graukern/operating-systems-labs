#pragma once
#include <sys/types.h>
#include <string>

namespace pidfile {

enum class Prev { None, Terminated, Failed };

long read(const std::string& path);
bool write(const std::string& path, pid_t pid);
bool processAlive(pid_t pid, const std::string& procRoot = "/proc");

Prev terminatePrevious(const std::string& path, pid_t self,
                       const std::string& procRoot = "/proc", int timeoutMs = 5000);

bool removeIfOwner(const std::string& path, pid_t self);

}  