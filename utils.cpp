#include "utils.h"

#include <sstream>

std::vector<std::string> Utils::Split(const std::string &s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream token_stream(s);

    // While the string hasn't ended, get a "line" from it ending with the delimeter
    while (std::getline(token_stream, token, delimiter))
    {
        // Save "line"/token in tokens vector
        tokens.push_back(token);
    }

    return tokens;
}