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

MyFs::myfs_dir_entry Utils::SearchFile(std::string &file_name, MyFs::dir_entries entries)
{
    // Go through the entries
    for (struct MyFs::myfs_dir_entry entry : entries)
    {
        // Check if the entry's name matches the file name
        if (std::string(entry.name) == file_name)
        {
            return entry;
        }
    }

    // Return an empty dir entry if not found
    return { 0 };
}

int Utils::CalcAmountOfBlocksForFile(uint32_t size)
{
    return int(size / BLOCK_DATA_SIZE) + (size % BLOCK_DATA_SIZE == 0 ? 0 : 1);
}