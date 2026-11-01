#pragma once
#include <cstdint>
#include <string>

namespace events {

std::string names(uint32_t mask);
std::string format(const std::string& dir, const std::string& name, uint32_t mask);

}  