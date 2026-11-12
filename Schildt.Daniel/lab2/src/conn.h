#pragma once

#include <cstddef>

const size_t CONN_MAX = 512;

class Conn {
public:
    Conn(int id, bool create);
    ~Conn();
    Conn(const Conn &) = delete;
    Conn &operator=(const Conn &) = delete;

    bool IsOpen() const;
    bool Read(void *buf, size_t count, int timeout_ms = 5000);
    bool Write(const void *buf, size_t count);
    bool TimedOut() const;

private:
    struct Impl;
    Impl *impl;
};
