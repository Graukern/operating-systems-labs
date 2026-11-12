#include "chat_client.h"
#include "chat.h"

#include <cstdio>
#include <poll.h>
#include <string>
#include <unistd.h>

static const char *NAME = "client";

static bool stdin_ready()
{
    pollfd p;
    p.fd = 0;
    p.events = POLLIN;
    p.revents = 0;
    return poll(&p, 1, 0) > 0;
}

static bool send_line(Conn &conn, const std::string &line)
{
    if (line == "/q") {
        Message bye = make_message(MSG_BYE, NAME, "");
        conn.Write(&bye, sizeof(bye));
        return false;
    }
    int type = MSG_PUBLIC;
    std::string text = line;
    if (line.compare(0, 3, "/p ") == 0) {
        type = MSG_PRIVATE;
        text = line.substr(3);
    }
    if (text.empty())
        return true;
    Message msg = make_message(type, NAME, text);
    if (!conn.Write(&msg, sizeof(msg))) {
        fprintf(stderr, "client: не удалось отправить сообщение\n");
        return false;
    }
    return true;
}

int run_client(int id, sem_t *ready)
{
    Conn conn(id, false);
    if (!conn.IsOpen()) {
        fprintf(stderr, "client: не удалось подключиться\n");
        return 1;
    }
    sem_post(ready);
    printf("client: подключился, /p текст - личное, /q - выход\n");
    fflush(stdout);

    std::string pending;
    while (true) {
        Message msg;
        if (conn.Read(&msg, sizeof(msg), 100)) {
            if (msg.type == MSG_BYE) {
                printf("client: хост завершил работу\n");
                return 0;
            }
            printf("%s\n", format_message(msg).c_str());
            fflush(stdout);
        } else if (!conn.TimedOut()) {
            fprintf(stderr, "client: связь с хостом потеряна\n");
            return 1;
        }

        if (!stdin_ready())
            continue;
        char buf[256];
        ssize_t n = read(0, buf, sizeof(buf));
        if (n <= 0) {
            send_line(conn, "/q");
            return 0;
        }
        pending.append(buf, n);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!send_line(conn, line))
                return 0;
        }
    }
}
