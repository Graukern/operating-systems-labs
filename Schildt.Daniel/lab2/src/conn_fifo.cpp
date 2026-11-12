#include "conn.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

struct Conn::Impl {
    int in = -1;
    int out = -1;
    std::string in_path;
    std::string out_path;
    bool create = false;
    bool timed_out = false;
};

static void fail(const char *what)
{
    fprintf(stderr, "conn_fifo: %s: %s\n", what, strerror(errno));
}

static int create_fifo(const std::string &path)
{
    unlink(path.c_str());
    if (mkfifo(path.c_str(), 0600) < 0) {
        fail(path.c_str());
        return -1;
    }
    int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0)
        fail(path.c_str());
    return fd;
}

static int open_fifo(const std::string &path)
{
    for (int i = 0; i < 100; i++) {
        int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd >= 0)
            return fd;
        if (errno != ENOENT)
            break;
        usleep(50000);
    }
    fail(path.c_str());
    return -1;
}

Conn::Conn(int id, bool create)
{
    impl = new Impl;
    impl->create = create;
    std::string base = "/tmp/lab2_fifo_" + std::to_string(id);
    std::string h2c = base + "_h2c";
    std::string c2h = base + "_c2h";
    if (create) {
        impl->out_path = h2c;
        impl->in_path = c2h;
        impl->out = create_fifo(h2c);
        impl->in = create_fifo(c2h);
    } else {
        impl->out_path = c2h;
        impl->in_path = h2c;
        impl->in = open_fifo(h2c);
        impl->out = open_fifo(c2h);
    }
}

Conn::~Conn()
{
    if (impl->in >= 0)
        close(impl->in);
    if (impl->out >= 0)
        close(impl->out);
    if (impl->create) {
        unlink(impl->in_path.c_str());
        unlink(impl->out_path.c_str());
    }
    delete impl;
}

bool Conn::IsOpen() const
{
    return impl->in >= 0 && impl->out >= 0;
}

bool Conn::TimedOut() const
{
    return impl->timed_out;
}

static int wait_fd(int fd, short events, int timeout_ms)
{
    pollfd p;
    p.fd = fd;
    p.events = events;
    p.revents = 0;
    int r;
    do {
        r = poll(&p, 1, timeout_ms);
    } while (r < 0 && errno == EINTR);
    return r;
}

bool Conn::Read(void *buf, size_t count, int timeout_ms)
{
    impl->timed_out = false;
    if (!IsOpen() || count > CONN_MAX)
        return false;
    int r = wait_fd(impl->in, POLLIN, timeout_ms);
    if (r == 0) {
        impl->timed_out = true;
        return false;
    }
    if (r < 0) {
        fail("poll");
        return false;
    }
    ssize_t n = read(impl->in, buf, count);
    if (n < 0) {
        fail("read");
        return false;
    }
    return (size_t)n == count;
}

bool Conn::Write(const void *buf, size_t count)
{
    impl->timed_out = false;
    if (!IsOpen() || count > CONN_MAX)
        return false;
    int r = wait_fd(impl->out, POLLOUT, 5000);
    if (r == 0) {
        impl->timed_out = true;
        return false;
    }
    if (r < 0) {
        fail("poll");
        return false;
    }
    ssize_t n = write(impl->out, buf, count);
    if (n < 0) {
        fail("write");
        return false;
    }
    return (size_t)n == count;
}
