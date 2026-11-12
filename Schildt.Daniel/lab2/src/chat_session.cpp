#include "chat_session.h"

ChatSession::ChatSession(Conn &conn, const std::string &name)
    : conn_(conn), name_(name)
{
}

ChatSession::~ChatSession()
{
    Stop();
}

void ChatSession::SetOnMessage(std::function<void(const Message &)> f)
{
    on_message_ = f;
}

void ChatSession::SetOnStatus(std::function<void(const std::string &)> f)
{
    on_status_ = f;
}

void ChatSession::SetIdle(int timeout_ms, std::function<void()> f)
{
    idle_ms_ = timeout_ms;
    on_idle_ = f;
}

void ChatSession::Status(const std::string &text)
{
    if (on_status_)
        on_status_(text);
}

void ChatSession::Start()
{
    active_ = true;
    last_rx_ = std::chrono::steady_clock::now();
    thread_ = std::thread(&ChatSession::Loop, this);
}

void ChatSession::Stop()
{
    if (!thread_.joinable())
        return;
    if (active_)
        Send(MSG_BYE, "");
    stop_ = true;
    thread_.join();
}

void ChatSession::Send(int type, const std::string &text)
{
    Message msg = make_message(type, name_, text);
    std::lock_guard<std::mutex> lock(mutex_);
    out_.push_back(msg);
}

bool ChatSession::Active() const
{
    return active_;
}

bool ChatSession::Flush()
{
    while (true) {
        Message msg;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (out_.empty())
                return true;
            msg = out_.front();
            out_.pop_front();
        }
        if (!conn_.Write(&msg, sizeof(msg))) {
            Status("не удалось отправить сообщение");
            return false;
        }
    }
}

bool ChatSession::Receive()
{
    Message msg;
    if (conn_.Read(&msg, sizeof(msg), 100)) {
        if (msg.type == MSG_BYE) {
            Status("собеседник вышел из чата");
            return false;
        }
        last_rx_ = std::chrono::steady_clock::now();
        if (on_message_)
            on_message_(msg);
        return true;
    }
    if (conn_.TimedOut())
        return true;
    Status("связь потеряна");
    return false;
}

bool ChatSession::Idle() const
{
    if (idle_ms_ <= 0)
        return false;
    auto passed = std::chrono::steady_clock::now() - last_rx_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(passed).count() > idle_ms_;
}

void ChatSession::Loop()
{
    while (!stop_) {
        if (!Receive())
            break;
        if (Idle()) {
            Status("клиент долго молчит, отключаем");
            if (on_idle_)
                on_idle_();
            break;
        }
        if (!Flush()) {
            while (Receive() && !conn_.TimedOut()) {
            }
            break;
        }
    }
    if (stop_)
        Flush();
    active_ = false;
}
