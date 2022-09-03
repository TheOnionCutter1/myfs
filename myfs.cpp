#include "myfs.h"

const char* MyFs::MYFS_MAGIC = "MYFS";

MyFs::MyFs(BlockDeviceSimulator* blkdevsim_) :
        blkdevsim(blkdevsim_), _parts(MyFs::_calcParts())
{
    struct myfs_header header{};

    blkdevsim->read(0, sizeof(header), (char*)&header);
    if (strncmp(header.magic, MYFS_MAGIC, sizeof(header.magic)) != 0 ||
        (header.version != CURR_VERSION))
    {
        std::cout << "Did not find myfs instance on blkdev" << std::endl;
        std::cout << "Creating..." << std::endl;
        format();
        std::cout << "Finished!" << std::endl;
    }
}

/**
 * Format the drive.
 * write the file system header, zero out the bitmaps and create root folder inode.
 */
void MyFs::format()
{
    struct myfs_header header{};
    int bitMapsSize = this->_parts.root - this->_parts.blockBitMap;
    auto* zeroesBuf = new uint8_t[bitMapsSize]{0};
    Inode root{};

    // put the header in place
    strncpy(header.magic, MYFS_MAGIC, sizeof(header.magic));
    header.version = CURR_VERSION;
    blkdevsim->write(0, sizeof(header), (const char*)&header);

    // zero out bit maps
    blkdevsim->write(this->_parts.blockBitMap, bitMapsSize, (const char*)zeroesBuf);
    delete[] zeroesBuf;

    // create root directory Inode
    root.directory = true;
    root.id = this->_allocateInode();
    blkdevsim->write(this->_parts.root, sizeof(root), (const char*)&root);
}

/**
 * Create a file in the root directory.
 * @param path_str the file's name.
 * @param directory whether the file is a directory. (directories are not yet implemented)
 */
void MyFs::create_file(const std::string& path_str, bool directory)
{
    const size_t LAST_DELIMITER = path_str.find_last_of('/');
    const std::string FILE_NAME = path_str.substr(LAST_DELIMITER + 1, std::string::npos);
    Inode file{};
    Inode dir = this->_getInode(path_str.substr(0, LAST_DELIMITER));
    DirEntry fileDetails{};

    // create file inode
    file.id = this->_allocateInode();
    file.directory = directory;
    this->_writeInode(file);

    // add the file to the directory that contains it
    // copy the name, leave 1 byte to the null terminator
    memcpy(fileDetails.name, FILE_NAME.c_str(), sizeof(fileDetails.name) - 1);
    fileDetails.id = file.id;
    this->_addFileToFolder(fileDetails, dir);
}

std::string MyFs::get_content(const std::string& path_str) const
{
    const Inode FILE = this->_getInode(path_str);
    const char* content = (char*)this->_readInodeData(FILE);
    std::string contentStr = content;

    delete[] content;

    return contentStr;
}

void MyFs::set_content(const std::string& path_str, const std::string& content)
{
    const size_t NEW_SIZE = content.size();
    const unsigned int LAST_POINTER = NEW_SIZE / BLOCK_SIZE;
    const char* cStrContent = content.c_str();
    Inode file = this->_getInode(path_str);
    unsigned int pointer{};
    int toWrite{};
    size_t bytesWritten{};

    file = this->_reallocateBlocks(file, NEW_SIZE);
    file.size = NEW_SIZE;
    while (bytesWritten != file.size)
    {
        toWrite = pointer == LAST_POINTER ?
                  (int)(file.size % BLOCK_SIZE) : BLOCK_SIZE;
        this->blkdevsim->write(file.addresses[pointer],
                               toWrite,
                               cStrContent + bytesWritten);
        bytesWritten += toWrite;
        pointer++;
    }
    this->_writeInode(file);
}

MyFs::dir_list MyFs::list_dir(const std::string& path_str)
{
    dir_list ans;
    dir_list_entry entry{};
    const Inode DIR = this->_getInode(path_str);
    const auto* rootDirContent = (DirEntry*)this->_readInodeData(DIR);
    Inode file{};
    size_t dirEntries{};

    dirEntries = DIR.size / sizeof(DirEntry);
    for (size_t i = 0; i < dirEntries; i++)
    {
        entry.name = rootDirContent[i].name;
        this->blkdevsim->read(this->_getInodeAddress(rootDirContent[i].id),
                              sizeof(Inode),
                              (char*)&file);
        entry.file_size = (int)file.size;
        entry.is_dir = file.directory;
        ans.push_back(entry);
    }
    delete[] rootDirContent;

    return ans;
}

/**
 * Calculate the disk parts for the file system.
 * @param deviceSize the disk device size.
 * @return a struct with pointers to every segment.
 */
constexpr MyFs::DiskParts MyFs::_calcParts()
{
    const int deviceSize = BlockDeviceSimulator::DEVICE_SIZE;
    DiskParts parts{};
    int remainingSpace = deviceSize - (int)sizeof(myfs_header);
    int amountOfBlocks = remainingSpace / BLOCK_SIZE;
    int amountOfInodes = 0;

    parts.blockBitMap = sizeof(myfs_header);
    parts.inodeBitMap = parts.blockBitMap;
    // make room for the blocks bit map
    while ((parts.inodeBitMap - parts.blockBitMap) * BITS_IN_BYTE < amountOfBlocks)
    {
        if ((parts.inodeBitMap - parts.blockBitMap) % BLOCK_SIZE == 0)
        {
            amountOfBlocks--;
        }
        parts.inodeBitMap++;
    }
    remainingSpace = deviceSize - parts.inodeBitMap;
    amountOfInodes = remainingSpace / BYTES_PER_INODE;
    parts.root = parts.inodeBitMap + (int)std::ceil((float)amountOfInodes / BITS_IN_BYTE);
    parts.unused = parts.root + amountOfInodes * (int)sizeof(Inode);

    parts.data = parts.unused + (deviceSize - parts.unused) % BLOCK_SIZE;

    return parts;
}

/**
 * Find an unoccupied bit in a bitmap and allocate it.
 * @param bitmapStart address of the start of the bitmap.
 * @return the index of the allocated bit in the bitmap.
 */
int MyFs::_allocate(int bitmapStart)
{
    constexpr int BITS_IN_BUFFER = 64;
    constexpr int BYTES_IN_BUFFER = BITS_IN_BUFFER / BITS_IN_BYTE;
    constexpr uint64_t ALL_OCCUPIED = 0xFFFFFFFFFFFFFFFF;
    uint64_t buffer = 0;
    int address = bitmapStart;

    // read the bitmap until an unoccupied memory is found
    do
    {
        this->blkdevsim->read(address, BYTES_IN_BUFFER, (char*)&buffer);
        address += BYTES_IN_BUFFER;
    } while (buffer == ALL_OCCUPIED);
    address -= BYTES_IN_BUFFER;

    // read the buffer until an unoccupied memory is found
    for (int i = 0; i < BITS_IN_BUFFER; i++)
    {
        if (!(buffer & (1 << i))) // if the (i)'s bit is 0
        {
            buffer ^= 1 << i; // flip the bit to mark as occupied
            this->blkdevsim->write(address, BYTES_IN_BUFFER, (const char*)&buffer);

            // get the index in the bitmap
            address -= bitmapStart;
            address += i;

            // once we found unoccupied space, we finished our task
            break;
        }
    }
    return address;
}

/**
 * Deallocate a bit from a bitmap.
 * @param bitmapStart address of the start of the bitmap.
 * @param n the index of the allocated bit in the bitmap.
 */
void MyFs::_deallocate(int bitmapStart, int n)
{
    int byteAddress = bitmapStart + n / BITS_IN_BYTE;
    int byte = 0;
    unsigned int offset = n % BITS_IN_BYTE;

    this->blkdevsim->read(byteAddress, 1, (char*)&byte);
    byte ^= 1 << offset; // flip the bit to mark as unoccupied
    this->blkdevsim->write(byteAddress, 1, (const char*)&byte);
}

/**
 * Resize the amount of blocks an inode points to.
 * Deallocate or allocate blocks according to the new size.
 * @param inode the inode's properties.
 * @param newSize the new intended size in bytes.
 * @return the same inode with updated pointers.
 */
MyFs::Inode MyFs::_reallocateBlocks(const MyFs::Inode& inode, size_t newSize)
{
    int i = 0;
    int usedBlocks{};
    size_t requiredBlocks = newSize / BLOCK_SIZE + (newSize % BLOCK_SIZE != 0);
    int blocksToAllocate{};
    Inode newAddresses = inode;

    if (requiredBlocks > DIRECT_POINTERS)
    {
        throw std::runtime_error("Error: reached maximum file size");
    }

    // check how many blocks are in use
    while (newAddresses.addresses[usedBlocks] != 0)
    {
        usedBlocks++;
    }
    blocksToAllocate = (int)(requiredBlocks - usedBlocks);
    // if we need to allocate
    if (blocksToAllocate > 0)
    {
        for (i = 0; i < blocksToAllocate; i++)
        {
            newAddresses.addresses[usedBlocks] = this->_allocateBlock();
            usedBlocks++;
        }
    }
    // if we need to deallocate
    else if (blocksToAllocate < 0)
    {
        for (i = 0; i < -blocksToAllocate; i++)
        {
            usedBlocks--;
            this->_deallocateBlock(newAddresses.addresses[usedBlocks]);
            newAddresses.addresses[usedBlocks] = 0;
        }
    }

    return newAddresses;
}

/**
 * Find an empty space for an inode and allocate it.
 * @return the inode's id.
 */
int MyFs::_allocateInode()
{
    return this->_allocate(this->_parts.inodeBitMap);
}

/**
 * Find an empty block and allocate it.
 * @return address of the block.
 */
int MyFs::_allocateBlock()
{
    int address = this->_allocate(this->_parts.blockBitMap);

    // get physical address of the occupied block
    address *= BLOCK_SIZE;
    address += this->_parts.data;

    return address;
}

/**
 * Deallocate a block of disk memory.
 * @param address the address of the block.
 */
void MyFs::_deallocateBlock(int address)
{
    int blockNumber = (address - this->_parts.data) / BLOCK_SIZE;

    this->_deallocate(this->_parts.blockBitMap, blockNumber);
}

/**
 * Get an inode's physical address.
 * @param id the inode's id.
 * @return the inode's address on disk.
 */
int MyFs::_getInodeAddress(int id) const
{
    return this->_parts.root + id * (int)sizeof(Inode);
}

/**
 * Add a file to a folder.
 * @param file the name and inode id of the file that will be written to disk.
 * @param folder the inode of the folder.
 */
void MyFs::_addFileToFolder(const MyFs::DirEntry& file, MyFs::Inode& folder)
{
    unsigned int pointer = folder.size / BLOCK_SIZE;
    int bytesLeft = sizeof(file);
    int address{};
    int spaceTakenInLastBlock = (int)(folder.size % BLOCK_SIZE);
    int emptySpace{};
    int toWrite{};
    size_t written{};

    folder = this->_reallocateBlocks(folder, folder.size + sizeof(file));
    address = folder.addresses[pointer] + spaceTakenInLastBlock;
    emptySpace = BLOCK_SIZE - spaceTakenInLastBlock;
    toWrite = bytesLeft > emptySpace ? emptySpace : bytesLeft;
    do
    {
        this->blkdevsim->write(
                address,
                toWrite,
                (char*)&file + written
        );
        folder.size += toWrite;
        written += toWrite;
        bytesLeft -= toWrite;
        pointer++;
        address = folder.addresses[pointer];

        // setup for next iteration
        toWrite = bytesLeft > BLOCK_SIZE ? BLOCK_SIZE : bytesLeft;
    } while (bytesLeft != 0);

    this->_writeInode(folder);
}

/**
 * Read the data an inode points to.
 * @param inode the inode.
 * @return the data that was read.
 */
void* MyFs::_readInodeData(const MyFs::Inode& inode) const
{
    const unsigned int LAST_POINTER = inode.size / BLOCK_SIZE;
    unsigned int pointer = 0;
    int toRead{};
    auto* buffer = new uint8_t[inode.size];
    size_t bytesRead{};

    while (bytesRead != inode.size)
    {
        toRead = pointer == LAST_POINTER ?
                 (int)(inode.size % BLOCK_SIZE) : BLOCK_SIZE;
        this->blkdevsim->read(inode.addresses[pointer],
                              toRead,
                              (char*)buffer + bytesRead);
        bytesRead += toRead;
        pointer++;
    }

    return buffer;
}

MyFs::Inode MyFs::_getRootDir() const
{
    Inode ans{};

    this->blkdevsim->read(this->_parts.root,
                          sizeof(Inode),
                          (char*)&ans);

    return ans;
}

/**
 * Get file's inode by path, assuming it exists
 * @param path the file's path.
 * @return the file's inode.
 */
MyFs::Inode MyFs::_getInode(std::string path) const
{
    size_t nextDelimiter = path.find('/');
    std::string nextFolder;
    Inode inode = this->_getRootDir();
    DirEntry* dirContent = nullptr;
    int index{};

    if (path == "/")
    {
        return inode; // return root dir
    }

    while (nextDelimiter != std::string::npos)
    {
        dirContent = (DirEntry*)this->_readInodeData(inode);
        path = path.substr(nextDelimiter + 1, std::string::npos);
        nextDelimiter = path.find('/');
        nextFolder = path.substr(0, nextDelimiter);
        while (strcmp(dirContent[index].name, nextFolder.c_str()) != 0)
        {
            index++;
        }
        this->blkdevsim->read(
                this->_getInodeAddress(dirContent[index].id),
                sizeof(Inode),
                (char*)&inode);
        index = 0;
        delete[] dirContent;
    }

    return inode;
}

/**
 * Write an inode to the disk.
 * @param inode the inode.
 */
void MyFs::_writeInode(const MyFs::Inode& inode)
{
    this->blkdevsim->write(this->_getInodeAddress(inode.id),
                           sizeof(inode),
                           (const char*)&inode);
}
