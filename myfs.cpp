#include "myfs.h"

#include <string.h>
#include <iostream>
#include <math.h>
#include <sstream>

#include "utils.h"
#include "myfs_exception.h"

const char *MyFs::MYFS_MAGIC = "MYFS";

MyFs::MyFs(BlockDeviceSimulator *blkdevsim_) : blkdevsim(blkdevsim_)
{
	struct myfs_header header;
	blkdevsim->read(0, sizeof(header), (char *)&header);

	if (strncmp(header.magic, MYFS_MAGIC, sizeof(header.magic)) != 0 ||
		(header.version != CURR_VERSION))
	{
		std::cout << "Did not find myfs instance on blkdev" << std::endl;
		std::cout << "Creating..." << std::endl;
		format();
		std::cout << "Finished!" << std::endl;
	}
}

void MyFs::format()
{
	struct myfs_header header;
	struct myfs_info sys_info = { 0 };

	struct myfs_entry rootFolderEntry;
	struct myfs_dir rootFolder = {0};

	struct myfs_entry empty_entry = { 0 };

	// put the header in place
	strncpy(header.magic, MYFS_MAGIC, sizeof(header.magic));
	header.version = CURR_VERSION;
	blkdevsim->write(0, sizeof(header), (const char *)&header);

	// Set the sys info after the header
	sys_info.inode_count = 1;
	sys_info.block_bitmap = 0b111111111; // Set all 9 first blocks as taken
	blkdevsim->write(sizeof(header), sizeof(sys_info), (const char *)&sys_info);

	// Pad the block of the header and the sys info
	blkdevsim->write(sizeof(header) + sizeof(sys_info), BLOCK_SIZE - sizeof(sys_info) - sizeof(header), std::string(BLOCK_SIZE - sizeof(sys_info) - sizeof(header), 0).c_str());

	// Set the root folder as first entry in the first entry in the inode table
	rootFolderEntry.inode = 1;
	rootFolderEntry.first_block = 8;
	rootFolderEntry.size = sizeof(rootFolder);
	rootFolderEntry.is_dir = true;
	blkdevsim->write(BLOCK_SIZE, sizeof(rootFolderEntry), (const char *)&rootFolderEntry);

	// Set empty entry after the first entry for the next file to be created to be set there
	blkdevsim->write(BLOCK_SIZE + sizeof(rootFolderEntry), sizeof(struct myfs_entry), (const char *)&empty_entry);

	// Set the root folder in the start of the drive
	blkdevsim->write(sizeof(header), sizeof(rootFolder), (const char *)&rootFolder);

	// Create structs for the current and root folder entries
	this->currentDirEntry = new struct myfs_entry(rootFolderEntry);
	this->rootFolderEntry = new struct myfs_entry(rootFolderEntry);
}

struct MyFs::myfs_entry MyFs::get_dir(const std::string &path_str)
{
	struct myfs_entry dir;
	struct myfs_dir_entry dir_entry;

	// Get all dirs names
	std::vector<std::string> dirs = Utils::Split(path_str, '/');
	dir_entries entries;

	// If the first dir of the path is the root folder set the dir as the entry of the first folder
	if (dirs[0].length() == 0)
	{
		dir = *rootFolderEntry;

		// Remove the first dir from the dirs list
		dirs.erase(dirs.begin());
	}

	// Go through the dir names in the dirs vector
	for (std::string& dir_name : dirs)
	{
		// Get the dir entries of the current dir
		entries = get_dir_entries(dir);

		// Try to find the dir as a file in the current dir
		dir_entry = Utils::SearchFile(dir_name, entries);
		if (dir_entry.inode == 0)
		{
			throw MyFsException("Unable to find the dir '" + dir_name + "'!");
		}

		// Try to get the entry of the dir
		dir = get_file_entry(dir_entry.inode);
		if (dir.inode == 0)
		{
			throw MyFsException("An error occurred while searching the dir's entry!");
		}
	}

	return dir;
}

MyFs::dir_entries MyFs::get_dir_entries(MyFs::myfs_entry dir_entry)
{
	dir_entries entries_vector;
	struct myfs_dir_entry *entries;
	struct myfs_dir dir;
	char *dir_data = new char[dir_entry.size];

	// Get all the dir data
	get_file(dir_entry, dir_data);

	// Get the dir struct from the dir data
	dir = *(struct myfs_dir *)dir_data;

	// Set the entries pointer to point at the start of them
	entries = (struct myfs_dir_entry *)(dir_data + sizeof(struct myfs_dir));

	// Push each entry of the dir into the vector of entries
	for(int i = 0; i < dir.amount; i++)
	{
		entries_vector.push_back(entries[i]);
	}
	
	// Release the memory allocated for the dir data
	delete[] dir_data;

	return entries_vector;
}

struct MyFs::myfs_entry MyFs::get_file_entry(const uint32_t inode)
{
	uint32_t entry_address = BLOCK_SIZE;
	struct myfs_entry entry = { 0 };

	for (uint32_t i = 0; i < (BLOCK_SIZE * INODE_TABLE_BLOCKS) / sizeof(struct myfs_entry); i++)
	{
		// Get the entry from the current entry address
		blkdevsim->read(entry_address, sizeof(struct myfs_entry), (char *)&entry);

		// If the inode matches the inode of the requested file, return the entry
		if (entry.inode == inode)
		{
			return entry;
		}

		// Set the address to point at the next entry
		entry_address += sizeof(struct myfs_entry);
	}

	return entry;
}

void MyFs::get_file(const myfs_entry file_entry, char *file_data)
{
	uint32_t file_pointer = 0;
	struct myfs_block block;

	// Set the next block to be taken as the first block of the file
	block.next_block = file_entry.first_block;

	// While there is a next block, keep on getting the blocks
	do {
		// Read the block
		blkdevsim->read(block.next_block * BLOCK_SIZE, BLOCK_SIZE, (char *)&block);

		// Copy the data from the block into the file data 
		memcpy(file_data + file_pointer, block.data, (file_entry.size - file_pointer) % BLOCK_DATA_SIZE);
	} while (block.next_block);
}

uint32_t MyFs::allocate_block(struct MyFs::myfs_block* block, struct MyFs::myfs_info *sys_info)
{
	uint32_t block_index = 1 + INODE_TABLE_BLOCKS;

	// While the block is allocated, continue to the next block
	while (block_index < sys_info->block_bitmap.size() && sys_info->block_bitmap.test(block_index))
	{
		block_index++;
	}

	// If can't find an empty block, send error
	if (block_index == sys_info->block_bitmap.size())
	{
		throw MyFsException("Hard drive full!");
	}

	// Allocate the block in the block's bitmap
	sys_info->block_bitmap.set(block_index);

	// Write the block struct to the newly allocated block
	blkdevsim->write(block_index * BLOCK_SIZE, BLOCK_SIZE, (const char *)block);

	return block_index;
}

void MyFs::create_file(std::string path_str, bool directory)
{
	throw MyFsException("not implemented");
}

std::string MyFs::get_content(std::string path_str)
{
	throw MyFsException("not implemented");
	return "";
}

void MyFs::set_content(std::string path_str, std::string content)
{
	throw MyFsException("not implemented");
}

MyFs::dir_list MyFs::list_dir(std::string path_str)
{
	dir_list ans;
	throw MyFsException("not implemented");
	return ans;
}
