#pragma once
#include <string>
#include <ctime>

class Event {
public:
    Event() = default;
    Event(std::time_t baseTime, char flag, const std::string& text);

    static bool parse(const std::string& line, Event& out);
    static long periodForFlag(char flag);          // 0 для однократных

    std::time_t nextOccurrenceAfter(std::time_t after) const;

    std::time_t baseTime() const { return baseTime_; }
    char flag() const { return flag_; }
    const std::string& text() const { return text_; }
    std::string key() const;

private:
    std::time_t baseTime_ = 0;
    char flag_ = '\0';
    std::string text_;
};