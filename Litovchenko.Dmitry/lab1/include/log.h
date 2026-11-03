#pragma once

#include <string>

namespace dlog {

void open(bool alsoStderr);
void close();

void info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void warning(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void event(const std::string& line);

}  // namespace dlog
