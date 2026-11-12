#pragma once

#include "conn.h"

#include <cstdint>
#include <string>

enum MsgType : int32_t {
    MSG_PUBLIC = 0,
    MSG_PRIVATE = 1,
    MSG_BYE = 2
};

struct Message {
    int32_t type;
    int64_t time_ms;
    char from[32];
    char text[400];
};

static_assert(sizeof(Message) <= CONN_MAX, "Message не влезает в Conn");

Message make_message(int type, const std::string &from, const std::string &text);
std::string format_message(const Message &msg);
