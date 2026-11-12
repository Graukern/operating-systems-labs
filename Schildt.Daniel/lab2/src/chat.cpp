#include "chat.h"

#include <cstdio>
#include <cstring>
#include <ctime>

Message make_message(int type, const std::string &from, const std::string &text)
{
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = type;
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    msg.time_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    strncpy(msg.from, from.c_str(), sizeof(msg.from) - 1);
    strncpy(msg.text, text.c_str(), sizeof(msg.text) - 1);
    return msg;
}

std::string format_message(const Message &msg)
{
    time_t sec = msg.time_ms / 1000;
    tm t;
    localtime_r(&sec, &t);
    char tbuf[16];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &t);
    std::string res = "[" + std::string(tbuf) + "] " + msg.from;
    if (msg.type == MSG_PRIVATE)
        res += " (лично)";
    return res + ": " + msg.text;
}
