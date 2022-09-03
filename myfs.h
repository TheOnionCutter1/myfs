#ifndef __MYFS_H__
#define __MYFS_H__

#include <memory>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "blkdev.h"

class MyFs {
public:
	MyFs(BlockDeviceSimulator *blkdevsim_);

	/**
	 * dir_list_entry struct
	 * This struct is used by list_dir method to return directory entry
	 * information.
	 */
	struct dir_list_entry {
		/**
		 * The directory entry name
		 */
		std::string name;

		/**
		 * whether the entry is a file or a directory
		 */
		bool is_dir;

		/**
		 * File size
		 */
		int file_size;
	};
	typedef std::vector<struct dir_list_entry> dir_list;

	/**
	 * format method
	 * This function discards the current content in the blockdevice and
	 * create a fresh new MYFS instance in the blockdevice.
	 */
	void format();

	/**
	 * create_file method
	 * Creates a new file in the required path.
	 * @param path_str the file path (e.g. "/newfile")
	 * @param directory boolean indicating whether this is a file or directory
	 */
	void create_file(const std::string& path_str, bool directory);

	/**
	 * get_content method
	 * Returns the whole content of the file indicated by path_str param.
	 * Note: this method assumes path_str refers to a file and not a
	 * directory.
	 * @param path_str the file path (e.g. "/somefile")
	 * @return the content of the file
	 */
	std::string get_content(const std::string& path_str) const;

	/**
	 * set_content method
	 * Sets the whole content of the file indicated by path_str param.
	 * Note: this method assumes path_str refers to a file and not a
	 * directory.
	 * @param path_str the file path (e.g. "/somefile")
	 * @param content the file content string
	 */
	void set_content(const std::string& path_str, const std::string& content);

	/**
	 * list_dir method
	 * Returns a list of a files in a directory.
	 * Note: this method assumes path_str refers to a directory and not a
	 * file.
	 * @param path_str the file path (e.g. "/somedir")
	 * @return a vector of dir_list_entry structures, one for each file in
	 *	the directory.
	 */
	dir_list list_dir(const std::string& path_str);

private:

	/**
	 * This struct represents the first bytes of a myfs filesystem.
	 * It holds some magic characters and a number indicating the version.
	 * Upon class construction, the magic and the header are tested - if
	 * they both exist than the file is assumed to contain a valid myfs
	 * instance. Otherwise, the blockdevice is formatted and a new instance is
	 * created.
	 */
	struct myfs_header {
		char magic[4];
		uint8_t version;
	};

    struct DiskParts
    {
        int blockBitMap; // pointer to the block bit map
        int inodeBitMap; // pointer to the Inode bit map
        int root; // pointer to where the inodes are stored
        int unused; // unused bytes
        int data; // pointer to where the data is stored
    };

    enum Constants
    {
        DIRECT_POINTERS=12,
        FILE_NAME_LEN=11,
        BLOCK_SIZE=16,
        BITS_IN_BYTE=8,
        BYTES_PER_INODE=16 * 1024 // an inode for every 16-KB of data
    };

    struct Inode
    {
        int id; // inode id
        bool directory;
        size_t size;
        int addresses[DIRECT_POINTERS];
    };

    struct DirEntry
    {
        char name[FILE_NAME_LEN];
        int id; // inode id
    };

	BlockDeviceSimulator* blkdevsim;
    const DiskParts _parts;

    int _getInodeAddress(int id) const;
    Inode _getRootDir() const;
    Inode _getInode(std::string path) const;

    void* _readInodeData(const Inode& inode) const;
    void _writeInode(const Inode& inode);
    void _addFileToFolder(const DirEntry& file, Inode& folder);

    int _allocate(int bitmapStart);
    void _deallocate(int bitmapStart, int n);
    Inode _reallocateBlocks(const Inode& inode, size_t newSize);
    int _allocateInode();
    int _allocateBlock();
    void _deallocateBlock(int address);

    static constexpr DiskParts _calcParts();

	static const uint8_t CURR_VERSION = 0x03;
	static const char* MYFS_MAGIC;
};

#endif // __MYFS_H__
