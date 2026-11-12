#include "conn.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct Conn::Impl {
    int fd = -1;
    std::string path;
    bool create = false;
    bool timed_out = false;
};

static void fail(const char *what)
{
    fprintf(stderr, "conn_sock: %s: %s\n", what, strerror(errno));
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

static sockaddr_un make_addr(const std::string &path)
{
    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    return addr;
}

static int accept_client(const std::string &path)
{
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) {
        fail("socket");
        return -1;
    }
    unlink(path.c_str());
    sockaddr_un addr = make_addr(path);
    if (bind(srv, (sockaddr *)&addr, sizeof(addr)) < 0 || listen(srv, 1) < 0) {
        fail(path.c_str());
        close(srv);
        return -1;
    }
    int fd = -1;
    if (wait_fd(srv, POLLIN, 5000) > 0)
        fd = accept(srv, nullptr, nullptr);
    else
        fprintf(stderr, "conn_sock: клиент не подключился за 5 секунд\n");
    close(srv);
    return fd;
}

static int connect_host(const std::string &path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fail("socket");
        return -1;
    }
    sockaddr_un addr = make_addr(path);
    for (int i = 0; i < 100; i++) {
        if (connect(fd, (sockaddr *)&addr, sizeof(addr)) == 0)
            return fd;
        if (errno != ENOENT && errno != ECONNREFUSED)
            break;
        usleep(50000);
    }
    fail(path.c_str());
    close(fd);
    return -1;
}

Conn::Conn(int id, bool create)
{
    impl = new Impl;
    impl->create = create;
    impl->path = "/tmp/lab2_sock_" + std::to_string(id);
    if (create)
        impl->fd = accept_client(impl->path);
    else
        impl->fd = connect_host(impl->path);
}

Conn::~Conn()
{
    if (impl->fd >= 0)
        close(impl->fd);
    if (impl->create)
        unlink(impl->path.c_str());
    delete impl;
}

bool Conn::IsOpen() const
{
    return impl->fd >= 0;
}

bool Conn::TimedOut() const
{
    return impl->timed_out;
}

bool Conn::Read(void *buf, size_t count, int timeout_ms)
{
    impl->timed_out = false;
    if (!IsOpen() || count > CONN_MAX)
        return false;
    char *p = (char *)buf;
    size_t got = 0;
    while (got < count) {
        int r = wait_fd(impl->fd, POLLIN, timeout_ms);
        if (r == 0) {
            impl->timed_out = true;
            return false;
        }
        if (r < 0) {
            fail("poll");
            return false;
        }
        ssize_t n = read(impl->fd, p + got, count - got);
        if (n < 0) {
            fail("read");
            return false;
        }
        if (n == 0)
            return false;
        got += n;
    }
    return true;
}

bool Conn::Write(const void *buf, size_t count)
{
    impl->timed_out = false;
    if (!IsOpen() || count > CONN_MAX)
        return false;
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < count) {
        int r = wait_fd(impl->fd, POLLOUT, 5000);
        if (r == 0) {
            impl->timed_out = true;
            return false;
        }
        if (r < 0) {
            fail("poll");
            return false;
        }
        ssize_t n = send(impl->fd, p + sent, count - sent, MSG_NOSIGNAL);
        if (n < 0) {
            fail("send");
            return false;
        }
        sent += n;
    }
    return true;
}
