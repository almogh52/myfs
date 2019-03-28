#pragma once

#include <string>
#include <vector>

#include "myfs.h"

class Utils
{
  public:
    static std::vector<std::string> Split(const std::string &s, char delimiter);
    static MyFs::myfs_dir_entry SearchFile(std::string file_name, MyFs::dir_entries entries);
    static MyFs::myfs_dir_entry SearchFile(uint32_t inode, MyFs::dir_entries entries);
    static int CalcAmountOfBlocksForFile(uint32_t size);
};