#pragma once

#include "chat.h"

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class ChatSession {
public:
    ChatSession(Conn &conn, const std::string &name);
    ~ChatSession();

    void SetOnMessage(std::function<void(const Message &)> f);
    void SetOnStatus(std::function<void(const std::string &)> f);

    void Start();
    void Stop();
    void Send(int type, const std::string &text);
    bool Active() const;

private:
    void Loop();
    bool Flush();
    bool Receive();
    void Status(const std::string &text);

    Conn &conn_;
    std::string name_;
    std::function<void(const Message &)> on_message_;
    std::function<void(const std::string &)> on_status_;
    std::thread thread_;
    std::atomic<bool> active_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::deque<Message> out_;
};
