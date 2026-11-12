#include "conn.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <mqueue.h>
#include <string>
#include <unistd.h>

struct Conn::Impl {
    mqd_t in = (mqd_t)-1;
    mqd_t out = (mqd_t)-1;
    std::string in_name;
    std::string out_name;
    bool create = false;
    bool timed_out = false;
};

static void fail(const char *what)
{
    fprintf(stderr, "conn_mq: %s: %s\n", what, strerror(errno));
}

static timespec deadline(int ms)
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    return ts;
}

static mqd_t create_queue(const std::string &name, int mode)
{
    mq_unlink(name.c_str());
    mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = CONN_MAX;
    mqd_t q = mq_open(name.c_str(), mode | O_CREAT | O_EXCL, 0600, &attr);
    if (q == (mqd_t)-1)
        fail(name.c_str());
    return q;
}

static mqd_t open_queue(const std::string &name, int mode)
{
    for (int i = 0; i < 100; i++) {
        mqd_t q = mq_open(name.c_str(), mode);
        if (q != (mqd_t)-1)
            return q;
        if (errno != ENOENT)
            break;
        usleep(50000);
    }
    fail(name.c_str());
    return (mqd_t)-1;
}

Conn::Conn(int id, bool create)
{
    impl = new Impl;
    impl->create = create;
    std::string base = "/lab2_mq_" + std::to_string(id);
    std::string h2c = base + "_h2c";
    std::string c2h = base + "_c2h";
    if (create) {
        impl->out_name = h2c;
        impl->in_name = c2h;
        impl->out = create_queue(h2c, O_WRONLY);
        impl->in = create_queue(c2h, O_RDONLY);
    } else {
        impl->out_name = c2h;
        impl->in_name = h2c;
        impl->in = open_queue(h2c, O_RDONLY);
        impl->out = open_queue(c2h, O_WRONLY);
    }
}

Conn::~Conn()
{
    if (impl->in != (mqd_t)-1)
        mq_close(impl->in);
    if (impl->out != (mqd_t)-1)
        mq_close(impl->out);
    if (impl->create) {
        mq_unlink(impl->in_name.c_str());
        mq_unlink(impl->out_name.c_str());
    }
    delete impl;
}

bool Conn::IsOpen() const
{
    return impl->in != (mqd_t)-1 && impl->out != (mqd_t)-1;
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
    char tmp[CONN_MAX];
    timespec ts = deadline(timeout_ms);
    ssize_t n = mq_timedreceive(impl->in, tmp, CONN_MAX, nullptr, &ts);
    if (n < 0) {
        if (errno == ETIMEDOUT)
            impl->timed_out = true;
        else
            fail("mq_timedreceive");
        return false;
    }
    if ((size_t)n != count)
        return false;
    memcpy(buf, tmp, count);
    return true;
}

bool Conn::Write(const void *buf, size_t count)
{
    impl->timed_out = false;
    if (!IsOpen() || count > CONN_MAX)
        return false;
    timespec ts = deadline(5000);
    if (mq_timedsend(impl->out, (const char *)buf, count, 0, &ts) < 0) {
        if (errno == ETIMEDOUT)
            impl->timed_out = true;
        else
            fail("mq_timedsend");
        return false;
    }
    return true;
}
