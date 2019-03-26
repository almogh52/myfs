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

	format();

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
	struct myfs_info sys_info = {0};

	struct myfs_entry rootFolderEntry = {0};
	struct myfs_dir rootFolder = {0};

	struct myfs_entry empty_entry = {0};

	// Fill all 8 first blocks with 0
	blkdevsim->write(0, (1 + INODE_TABLE_BLOCKS) * BLOCK_SIZE, std::string((1 + INODE_TABLE_BLOCKS) * BLOCK_SIZE, 0).c_str());

	// put the header in place
	strncpy(header.magic, MYFS_MAGIC, sizeof(header.magic));
	header.version = CURR_VERSION;
	blkdevsim->write(0, sizeof(header), (const char *)&header);

	// Set the sys info after the header
	sys_info.inode_count = 1;
	sys_info.block_bitmap = 0b111111111; // Set all 9 first blocks as taken
	blkdevsim->write(sizeof(header), sizeof(sys_info), (const char *)&sys_info);

	// Set the root folder as first entry in the first entry in the inode table
	rootFolderEntry.inode = 1;
	rootFolderEntry.first_block = 8;
	rootFolderEntry.size = sizeof(rootFolder);
	rootFolderEntry.is_dir = true;
	blkdevsim->write(BLOCK_SIZE, sizeof(rootFolderEntry), (const char *)&rootFolderEntry);

	// Set empty entry after the first entry for the next file to be created to be set there
	blkdevsim->write(BLOCK_SIZE + sizeof(rootFolderEntry), sizeof(struct myfs_entry), (const char *)&empty_entry);

	// Set the root folder in the start of the drive
	blkdevsim->write(INODE_TABLE_BLOCKS * BLOCK_SIZE, sizeof(rootFolder), (const char *)&rootFolder);

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
	for (std::string &dir_name : dirs)
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
	for (int i = 0; i < dir.amount; i++)
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
	struct myfs_entry entry = {0};

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
	do
	{
		// Read the block
		blkdevsim->read(block.next_block * BLOCK_SIZE, BLOCK_SIZE, (char *)&block);

		// Copy the data from the block into the file data
		memcpy(file_data + file_pointer, block.data, (file_entry.size - file_pointer) % BLOCK_DATA_SIZE);
	} while (block.next_block);
}

uint32_t MyFs::allocate_block(struct MyFs::myfs_block *block, struct MyFs::myfs_info *sys_info)
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

void MyFs::add_entry(struct MyFs::myfs_entry *file_entry)
{
	struct myfs_entry entry = {0};
	uint32_t entry_table_pointer = BLOCK_SIZE;

	// While we didn't find an empty entry
	do
	{
		// Read the entry from the entries table
		blkdevsim->read(entry_table_pointer, sizeof(entry), (char *)&entry);

		// Point to the next entry
		entry_table_pointer += sizeof(entry);
	} while (entry.inode != 0 && (entry_table_pointer + sizeof(entry)) < (1 + INODE_TABLE_BLOCKS) * BLOCK_SIZE);

	// If the pointer is after the end of the table, throw error
	if ((entry_table_pointer + sizeof(entry)) > (1 + INODE_TABLE_BLOCKS) * BLOCK_SIZE)
	{
		throw MyFsException("Inode entries table is full!");
	}

	// Write the new entry
	blkdevsim->write(entry_table_pointer - sizeof(struct myfs_entry), sizeof(struct myfs_entry), (const char *)file_entry);
}

void MyFs::update_entry(struct MyFs::myfs_entry *file_entry)
{
	struct myfs_entry entry = {0};
	uint32_t entry_table_pointer = BLOCK_SIZE;

	// While we didn't find the entry in the table
	do
	{
		// Read the entry from the entries table
		blkdevsim->read(entry_table_pointer, sizeof(entry), (char *)&entry);

		// Point to the next entry
		entry_table_pointer += sizeof(entry);
	} while (entry.inode != file_entry->inode && (entry_table_pointer + sizeof(entry)) < (1 + INODE_TABLE_BLOCKS) * BLOCK_SIZE);

	// If the entry wasn't found, throw error
	if (entry.inode != file_entry->inode)
	{
		throw MyFsException("Inode entry wasn't found!");
	}

	// Write the new entry
	blkdevsim->write(entry_table_pointer - sizeof(struct myfs_entry), sizeof(struct myfs_entry), (const char *)file_entry);
}

void MyFs::update_file(struct MyFs::myfs_entry *file_entry, char *data, uint32_t size)
{
	uint32_t data_pointer = size - size % BLOCK_DATA_SIZE, block_index = 0;
	struct myfs_info sys_info = {0};
	struct myfs_block block = {0};

	// Get the file system info struct
	blkdevsim->read(sizeof(myfs_header), sizeof(sys_info), (char *)&sys_info);

	// If the file isn't allocated yet, allocate it
	if (file_entry->first_block == 0 && size != 0)
	{
		// While we didn't reach the end of the data (backwards addition of the file)
		while (data_pointer)
		{
			// Set the next block's index
			block.next_block = block_index;

			// Copy the current block's data to the block's struct
			memcpy(block.data, data + data_pointer, (size - data_pointer) % BLOCK_DATA_SIZE);

			// Allocate the block and get it's position
			block_index = allocate_block(&block, &sys_info);

			// Go to the next block
			data_pointer -= BLOCK_DATA_SIZE;
		}

		// Set the first block as we received from the loop
		file_entry->first_block = block_index;
	}
	// If the file has blocks on memory and has changed, rewrite all the blocks
	else if (file_entry->first_block != 0 && size != 0)
	{
		data_pointer = 0;
		block_index = file_entry->first_block;

		// While we didn't reach the end of our existing block chain
		while (data_pointer < size && data_pointer < Utils::CalcAmountOfBlocksForFile(file_entry->size) * BLOCK_DATA_SIZE)
		{
			// Get the next block of the file
			blkdevsim->read(block_index * BLOCK_SIZE, BLOCK_SIZE, (char *)&block);

			// Copy the current block's data to the block's struct
			memcpy(block.data, data + data_pointer, (size - data_pointer) % BLOCK_DATA_SIZE);

			// Overwrite the block
			blkdevsim->write(block_index * BLOCK_SIZE, BLOCK_SIZE, (const char *)&block);

			// Move to the next block
			block_index = block.next_block;

			// Go to the next block
			data_pointer += BLOCK_DATA_SIZE;
		}
	}

	// Set the size of the file
	file_entry->size = size;

	// Update the file entry in the inode entries table
	update_entry(file_entry);

	// Overwrite the file system info structure
	blkdevsim->write(sizeof(struct myfs_header), sizeof(sys_info), (const char *)&sys_info);
}

void MyFs::add_dir_entry(struct MyFs::myfs_entry *dir, struct MyFs::myfs_entry *file_entry, std::string file_name)
{
	struct myfs_dir_entry file_dir_entry = {0};
	struct myfs_dir *dir_ptr;
	char *new_dir_data = new char[dir->size + sizeof(struct myfs_dir_entry)];

	// If a file with the file name already exists throw error
	if (Utils::SearchFile(file_name, get_dir_entries(*dir)).inode != 0)
	{
		throw MyFsException("File with the name " + file_name + " already exists!");
	}

	// Set the dir entry properties
	file_dir_entry.inode = file_entry->inode;
	strcpy(file_dir_entry.name, file_name.c_str());

	// Read the existing dir data into the new dir data array
	get_file(*dir, new_dir_data);

	// Increase the file amount
	dir_ptr = (struct myfs_dir *)new_dir_data;
	dir_ptr->amount++;

	memcpy(new_dir_data + dir->size, &file_dir_entry, sizeof(file_dir_entry));

	// Update the dir file
	update_file(dir, new_dir_data, dir->size + sizeof(struct myfs_dir_entry));
}

struct MyFs::myfs_entry MyFs::allocate_file(bool is_dir)
{
	struct myfs_entry file_entry = {0};
	struct myfs_info sys_info = {0};

	// Get the file system info struct
	blkdevsim->read(sizeof(myfs_header), sizeof(sys_info), (char *)&sys_info);

	// Increase the inode counter
	sys_info.inode_count += 1;

	// Set file's properties
	file_entry.inode = sys_info.inode_count;
	file_entry.is_dir = is_dir;

	// Add the entry to inode table
	add_entry(&file_entry);

	// Overwrite the file system info structure
	blkdevsim->write(sizeof(struct myfs_header), sizeof(sys_info), (const char *)&sys_info);

	return file_entry;
}

void MyFs::create_file(std::string path, std::string file_name)
{
	struct myfs_entry dir, file;

	// Get the dir from the path
	dir = get_dir(path);

	// Allocate the file
	file = allocate_file(false);

	// Add a dir entry for the file in the dir file
	add_dir_entry(&dir, &file, file_name);

	// Update current dir for changes
	update_dirs_ptrs();
}

void MyFs::update_dirs_ptrs()
{
	uint32_t current_dir_inode = currentDirEntry->inode;

	// Release current pointers
	delete currentDirEntry;
	delete rootFolderEntry;

	// Re-create entries with data from disk
	currentDirEntry = new struct myfs_entry(get_file_entry(current_dir_inode));
	rootFolderEntry = new struct myfs_entry(get_file_entry(1));
}

void MyFs::create_file(std::string path_str, bool directory)
{
	std::vector<std::string> tokens;

	if (!directory)
	{
		tokens = Utils::Split(path_str, '/');

		path_str = path_str.substr(0, path_str.size() - tokens.back().length());

		create_file(path_str, tokens.back());
	}
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
