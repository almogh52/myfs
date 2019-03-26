#include "myfs.h"

#include <string.h>
#include <iostream>
#include <math.h>
#include <sstream>

#include "utils.h"

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
	uint32_t amount_of_inodes = 1;
	struct myfs_header header;
	struct myfs_entry rootFolderEntry;
	struct myfs_dir rootFolder = {0};

	// put the header in place
	strncpy(header.magic, MYFS_MAGIC, sizeof(header.magic));
	header.version = CURR_VERSION;
	blkdevsim->write(0, sizeof(header), (const char *)&header);

	// Set the amount of inodes in the last position of the block device
	blkdevsim->write(BlockDeviceSimulator::DEVICE_SIZE - sizeof(uint32_t), sizeof(uint32_t), (const char *)&amount_of_inodes);

	// Set the root folder as first entry in the first entry in the inode table
	rootFolderEntry.inode = 1;
	rootFolderEntry.address = sizeof(header); // Set it after the header
	blkdevsim->write(BlockDeviceSimulator::DEVICE_SIZE - sizeof(rootFolderEntry) - sizeof(uint32_t), sizeof(rootFolderEntry), (const char *)&rootFolderEntry);

	// Set the root folder in the start of the drive
	blkdevsim->write(sizeof(header), sizeof(rootFolder), (const char *)&rootFolder);

	// Set the current dir as the root folder
	this->currentDir = new struct myfs_dir(rootFolder);
	this->currentDirEntry = new struct myfs_entry(rootFolderEntry);
}

struct MyFs::myfs_entry MyFs::get_file_entry(const uint32_t inode)
{
	uint32_t amount_of_inodes;
	uint32_t entry_address = BlockDeviceSimulator::DEVICE_SIZE - sizeof(uint32_t) - sizeof(struct myfs_entry);
	struct myfs_entry entry = { 0 };

	// Get the amount of inodes from the end of the block device
	blkdevsim->read(BlockDeviceSimulator::DEVICE_SIZE - sizeof(uint32_t), sizeof(uint32_t), (char *)&amount_of_inodes);

	for (uint32_t i = 0; i < amount_of_inodes; i++)
	{
		// Get the entry from the current entry address
		blkdevsim->read(entry_address, sizeof(struct myfs_entry), (char *)&entry);

		// If the inode matches the inode of the requested file, return the entry
		if (entry.inode == inode)
		{
			return entry;
		}

		// Set the address to point at the next entry
		entry_address -= sizeof(struct myfs_entry);
	}

	return entry;
}

void MyFs::create_file(std::string path_str, bool directory)
{
	throw std::runtime_error("not implemented");
}

std::string MyFs::get_content(std::string path_str)
{
	throw std::runtime_error("not implemented");
	return "";
}

void MyFs::set_content(std::string path_str, std::string content)
{
	throw std::runtime_error("not implemented");
}

MyFs::dir_list MyFs::list_dir(std::string path_str)
{
	dir_list ans;
	throw std::runtime_error("not implemented");
	return ans;
}
