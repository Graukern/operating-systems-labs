#pragma once
#include <iosfwd>
#include <string>
#include <vector>

namespace config {

struct Result {
    std::vector<std::string> dirs;      
    std::vector<std::string> warnings; 
};

std::string trim(const std::string& s);

Result parse(std::istream& in);

bool loadFile(const std::string& path, Result& out);
} 