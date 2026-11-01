#include "Event.h"
#include <sstream>
#include <cstring>

Event::Event(std::time_t baseTime, char flag, const std::string& text)
    : baseTime_(baseTime), flag_(flag), text_(text) {}

long Event::periodForFlag(char flag) {
    switch (flag) {
        case 'h': return 3600;
        case 'd': return 86400;
        case 'w': return 604800;
        default:  return 0;
    }
}

bool Event::parse(const std::string& line, Event& out) {
    std::istringstream iss(line);
    std::string dateStr, timeStr;
    if (!(iss >> dateStr >> timeStr)) return false;

    std::string token;
    iss >> token;
    if (token.empty()) return false;

    char flag = '\0';
    std::string text;
    if (token[0] == '-') {
        if (token.size() < 2) return false;
        flag = token[1];
        if (flag != 'h' && flag != 'd' && flag != 'w') return false;
        std::getline(iss, text);
    } else {
        text = token;
        std::string rest;
        std::getline(iss, rest);
        text += rest;
    }

    auto pos = text.find_first_not_of(" \t");
    if (pos == std::string::npos) return false;
    text = text.substr(pos);

    std::string dt = dateStr + " " + timeStr;
    std::tm tm = {};
    if (strptime(dt.c_str(), "%Y-%m-%d %H:%M:%S", &tm) == nullptr) return false;
    tm.tm_isdst = -1;

    int year = tm.tm_year, month = tm.tm_mon, day = tm.tm_mday;
    int hour = tm.tm_hour, minute = tm.tm_min, second = tm.tm_sec;

    std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return false;

    // Отсекаем 2025-02-30 → 2025-03-02 и подобное
    std::tm check = {};
    localtime_r(&t, &check);
    if (check.tm_year != year || check.tm_mon  != month || check.tm_mday != day ||
        check.tm_hour != hour || check.tm_min  != minute || check.tm_sec != second)
        return false;

    out = Event(t, flag, text);
    return true;
}

std::time_t Event::nextOccurrenceAfter(std::time_t after) const {
    if (baseTime_ == 0) return 0;
    if (baseTime_ > after) return baseTime_;

    long period = periodForFlag(flag_);
    if (period == 0) return 0;

    std::time_t diff = after - baseTime_;
    long n = diff / period + 1;
    return baseTime_ + n * period;
}

std::string Event::key() const {
    return std::to_string(baseTime_) + "|" + std::string(1, flag_) + "|" + text_;
}