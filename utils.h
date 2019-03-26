#pragma once

#include <string>
#include <vector>

class Utils
{
  public:
    static std::vector<std::string> Split(const std::string &s, char delimiter);
};